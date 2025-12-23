#include <chrono>
#include <thread>
#include <filesystem>
#include <gtest/gtest.h>
#include <cstdarg>
#include <arrow/api.h>
#include <parquet/arrow/writer.h>
#include <arrow/io/file.h>

#include "schema.h"
#include "instance.h"
#include "storage-buffer.h"
#include "service-storage.h"

Schema schema;
std::unique_ptr<StorageManager> man;
std::unique_ptr<StorageManager> man_parquet;
std::shared_ptr<ParquetBackend> parquet_backend;

enum class Color { RED, GREEN, YELLOW, RESET };
inline const char* ColorCode(Color c) {
    switch (c) {
    case Color::RED: return "\x1B[31m";
    case Color::GREEN: return "\x1B[32m";
    case Color::YELLOW: return "\x1B[33m";
    default: return "\x1B[0m";
    }
}
inline void ColoredPrint(Color c, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::cout << ColorCode(c);
    vprintf(fmt, args);
    std::cout << ColorCode(Color::RESET);
    va_end(args);
}

TEST(Storage, CreateSchema) {
    schema.add_field("field1", Kind::Int32);
    schema.add_field("field2", Kind::Int32);
    schema.add_field("field3", Kind::Int64);
    schema.add_field("field4", Kind::Int64);
    schema.finalise();
    ASSERT_TRUE(schema.isfinalised());
    ColoredPrint(Color::YELLOW, "instance size = %d\n", schema.instance_size());
}

TEST(Storage, CreateBuffer) {
	StreamBuffer buffer(schema, 20);
    ASSERT_NE(&buffer, nullptr);
}

TEST(StreamBuffer, AppendSingleRecords) {
    size_t recordSize = schema.instance_size();
    StreamBuffer buffer(schema, 20);

    // Prepare a valid record
    std::vector<std::byte> record(recordSize);
    for (size_t i = 0; i < recordSize; i++) {
        record[i] = static_cast<std::byte>(i % 256);
    }

    auto res = buffer.append(std::move(record));
    ASSERT_TRUE(res.appended && !res.overwritten) << "Append should succeed for valid record size";
    ASSERT_EQ(buffer.size(), 1) << "Buffer count should be 1 after append";
    res = buffer.append(std::move(record));
    ASSERT_TRUE(res.appended && !res.overwritten) << "Append should succeed for valid record size";
    ASSERT_EQ(buffer.size(), 2) << "Buffer count should be 2 after append";
}

TEST(StreamBuffer, AppendInvalidSize) {
    size_t recordSize = schema.instance_size();
	StreamBuffer buffer(schema, 20);

    // Prepare a record with incorrect size
    std::vector<std::byte> badRecord(recordSize - 1);
    auto res = buffer.append(std::move(badRecord));
    ASSERT_FALSE(res.had_rejection()) << "Append should fail for invalid record size";
    ASSERT_EQ(buffer.size(), 0);
}

TEST(StreamBuffer, AppendUntilFull) {
    size_t recordSize = schema.instance_size();
    size_t capacityRecords = 4; // small capacity for test
	StreamBuffer buffer(schema, capacityRecords);

    std::vector<std::byte> record(recordSize, std::byte{ 0xAA });

    // Fill buffer
    for (size_t i = 0; i < capacityRecords; i++) {
        ASSERT_TRUE(buffer.append(std::vector<std::byte>(record))) << "Append should succeed until full";
    }

    // Next append should fail due to full buffer
    ASSERT_FALSE(buffer.append(std::vector<std::byte>(record))) << "Append should fail when buffer is full";
    ASSERT_EQ(buffer.size(), capacityRecords);
}

TEST(Manager, CreateManager) {
    man = std::make_unique<StorageManager>();
    ASSERT_NE(man, nullptr);
}

TEST(Manager, AddStreamWithFlush) {
    bool ok = man->create_stream(
        "teststream",
        StreamStorageOptions{},
        schema
    );
    ASSERT_TRUE(ok);
}

TEST(Manager, ProducerTokenSubmission) {
    // Acquire producer token
    auto tokenOpt = man->get_producer_token("teststream");
    ASSERT_TRUE(tokenOpt.has_value()) << "Failed to get producer token";

    ProducerToken token = tokenOpt.value();

    // Prepare user payload (full schema-defined fields, no timestamp)
    size_t userRecordSize = schema.instance_size();
    std::vector<std::byte> payload(userRecordSize);

    Instance instance(schema);
    instance.set<int32_t>("field1", 12345);
    instance.set<int32_t>("field2", 24680);
    instance.set<int64_t>("field3", 54321);
    instance.set<int64_t>("field4", 13579);

    // Copy only the field data (StorageManager will add timestamp)
    std::memcpy(payload.data(), instance.get_data(), userRecordSize);

    // Submit multiple batches via token
    for (int i = 0; i < 1000; i++) {
        auto result = token.try_submit(std::vector<std::byte>(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
    }

    // Validate stored record count
	auto records = man->stream_size("teststream").value();
    ColoredPrint(Color::YELLOW, "teststream records stored = %d\n", records);
    ASSERT_EQ(records, 1000);

    auto result = token.try_submit(std::vector<std::byte>(payload));
    ASSERT_EQ(result, SubmitResult::BackPressure);

    // Validate stored record count
    records = man->stream_size("teststream").value();
    ColoredPrint(Color::YELLOW, "teststream records stored = %d\n", records);
    ASSERT_EQ(records, 1000);
}

TEST(ParquetWriteTest, CreateSimpleFile) {
    arrow::Int32Builder int_builder;
    arrow::StringBuilder str_builder;

    ASSERT_TRUE(int_builder.Append(1).ok());
    ASSERT_TRUE(int_builder.Append(2).ok());
    ASSERT_TRUE(int_builder.Append(3).ok());

    ASSERT_TRUE(str_builder.Append("alpha").ok());
    ASSERT_TRUE(str_builder.Append("beta").ok());
    ASSERT_TRUE(str_builder.Append("gamma").ok());

    std::shared_ptr<arrow::Array> int_array;
    std::shared_ptr<arrow::Array> str_array;
    ASSERT_TRUE(int_builder.Finish(&int_array).ok());
    ASSERT_TRUE(str_builder.Finish(&str_array).ok());

    auto schema = arrow::schema({
        arrow::field("numbers", arrow::int32()),
        arrow::field("words", arrow::utf8())
        });

    auto table = arrow::Table::Make(schema, { int_array, str_array });

    // Use absolute path to avoid working directory issues
    std::filesystem::path parquet_path = std::filesystem::absolute("test_output.parquet");
    ColoredPrint(Color::YELLOW, "Writing Parquet file to: %s", parquet_path.c_str());

    auto maybe_outfile = arrow::io::FileOutputStream::Open(parquet_path.string());
    ASSERT_TRUE(maybe_outfile.ok()) << maybe_outfile.status().ToString();
    auto outfile = *maybe_outfile;

    auto status = parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile, 1024);
    ASSERT_TRUE(status.ok()) << status.ToString();

    ASSERT_TRUE(outfile->Close().ok());

    ASSERT_TRUE(std::filesystem::exists(parquet_path)) << "Parquet file was not created at " << parquet_path;
}

TEST(Manager_Parquet, CreateManager) {
	parquet_backend = std::make_shared<ParquetBackend>("parquet_data", schema);
	man_parquet = std::make_unique<StorageManager>(parquet_backend);
    ASSERT_NE(man_parquet, nullptr);
}

TEST(Manager_Parquet, AddStreamWithFlush) {
    bool ok = man_parquet->create_stream(
        "teststream",
        StreamStorageOptions{
			.capacity_records = 10000,
            .flush_batch_size = 5000
        },
        schema
    );
    ASSERT_TRUE(ok);
}

TEST(Manager_Parquet, ProducerTokenSubmission) {
    // Acquire producer token
    auto tokenOpt = man_parquet->get_producer_token("teststream");
    ASSERT_TRUE(tokenOpt.has_value()) << "Failed to get producer token";

    ProducerToken token = tokenOpt.value();

    // Prepare user payload (full schema-defined fields, no timestamp)
    size_t userRecordSize = schema.instance_size();
    std::vector<std::byte> payload(userRecordSize);

    Instance instance(schema);
    instance.set<int32_t>("field1", 12345);
    instance.set<int32_t>("field2", 24680);
    instance.set<int64_t>("field3", 54321);
    instance.set<int64_t>("field4", 13579);

    // Copy only the field data (StorageManager will add timestamp)
    std::memcpy(payload.data(), instance.get_data(), userRecordSize);

    // Submit multiple batches via token
    for (int i = 0; i < 10000; i++) {
        auto result = token.try_submit(std::vector<std::byte>(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
    }

    // Validate stored record count
    auto records = man_parquet->stream_size("teststream").value();
    ColoredPrint(Color::YELLOW, "teststream records stored = %d\n", records);
    ASSERT_EQ(records, 10000);
}

TEST(Manager_Parquet, ForceFlush) {
	man_parquet->flush_stream("teststream");
}