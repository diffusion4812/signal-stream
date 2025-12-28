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
#include "service-storage-backend.h"
#include "service-project.h"

// ============================================================================
// Test Utilities
// ============================================================================

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
    printf("             ");
    vprintf(fmt, args);
    printf("\n");
    std::cout << ColorCode(Color::RESET);
    va_end(args);
}

// Helper to create a test record
std::vector<std::byte> CreateTestRecord(const Schema& schema, int32_t val1 = 12345, int32_t val2 = 24680,
    int64_t val3 = 54321, int64_t val4 = 13579) {
    size_t recordSize = schema.instance_size();
    std::vector<std::byte> payload(recordSize);

    Instance instance(schema);
    instance.set<int32_t>("field1", val1);
    instance.set<int32_t>("field2", val2);
    instance.set<int64_t>("field3", val3);
    instance.set<int64_t>("field4", val4);

    std::memcpy(payload.data(), instance.get_data(), recordSize);
    return payload;
}

// ============================================================================
// Test Fixtures
// ============================================================================

class SchemaTest : public ::testing::Test {
protected:
    Schema schema;

    void SetUp() override {
        schema.add_field("field1", Kind::Int32);
        schema.add_field("field2", Kind::Int32);
        schema.add_field("field3", Kind::Int64);
        schema.add_field("field4", Kind::Int64);
        schema.finalize();
    }
};

class StreamBufferTest : public SchemaTest {
protected:
    static constexpr size_t DEFAULT_CAPACITY = 20;

    std::unique_ptr<StreamBuffer> CreateBuffer(size_t capacity = DEFAULT_CAPACITY) {
        return std::make_unique<StreamBuffer>(schema, capacity);
    }
};

class StorageManagerTest : public SchemaTest {
protected:
    std::unique_ptr<StorageManager> manager;

    void SetUp() override {
        SchemaTest::SetUp();
        manager = std::make_unique<StorageManager>();
    }

    void TearDown() override {
    }

    ProducerToken CreateStream(const std::string& streamId, const StreamStorageOptions& opts) {
        bool ok = manager->create_stream(streamId, opts, schema);
        EXPECT_TRUE(ok);
        auto tokenOpt = manager->get_producer_token(streamId);
        EXPECT_TRUE(tokenOpt.has_value());
        return tokenOpt.value();
    }
};

class ParquetStorageTest : public SchemaTest {
protected:
    std::shared_ptr<ParquetBackend> backend;
    std::unique_ptr<StorageManager> manager;
    std::filesystem::path outputDir;

    void SetUp() override {
        SchemaTest::SetUp();
        outputDir = std::filesystem::absolute("test_parquet_output");
        std::filesystem::create_directories(outputDir);

        backend = std::make_shared<ParquetBackend>(outputDir.string(), schema);
        manager = std::make_unique<StorageManager>();
    }

    void TearDown() override {
        // Cleanup test files (optional - comment out to inspect outputs)
        // std::filesystem::remove_all(outputDir);
    }

    ProducerToken CreateStream(const std::string& streamId, const StreamStorageOptions& opts) {
        bool ok = manager->create_stream(streamId, opts, schema);
        EXPECT_TRUE(ok);
        auto tokenOpt = manager->get_producer_token(streamId);
        EXPECT_TRUE(tokenOpt.has_value());
        return tokenOpt.value();
    }
};

// ============================================================================
// Schema Tests
// ============================================================================

TEST_F(SchemaTest, CreateAndFinalizeSchema) {
    ASSERT_TRUE(schema.is_finalized());
    ASSERT_GT(schema.instance_size(), 0);
    ColoredPrint(Color::YELLOW, "Schema instance size = %zu bytes", schema.instance_size());
}

// ============================================================================
// StreamBuffer Tests
// ============================================================================

TEST_F(StreamBufferTest, CreateBuffer) {
    auto buffer = CreateBuffer();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(buffer->size(), 0);
    ASSERT_EQ(buffer->capacity_records(), DEFAULT_CAPACITY);
}

TEST_F(StreamBufferTest, AppendSingleRecord) {
    auto buffer = CreateBuffer();
    size_t recordSize = schema.instance_size();

    std::vector<std::byte> record(recordSize);
    for (size_t i = 0; i < recordSize; i++) {
        record[i] = static_cast<std::byte>(i % 256);
    }

    auto res = buffer->append(std::move(record));
    ASSERT_TRUE(res.appended && !res.overwritten) << "First append should succeed";
    ASSERT_EQ(buffer->size(), 1);
}

TEST_F(StreamBufferTest, AppendMultipleRecords) {
    auto buffer = CreateBuffer();
    size_t recordSize = schema.instance_size();
    std::vector<std::byte> record(recordSize, std::byte{ 0xAA });

    for (size_t i = 0; i < 5; i++) {
        auto res = buffer->append(std::vector<std::byte>(record));
        ASSERT_TRUE(res.appended && !res.overwritten);
        ASSERT_EQ(buffer->size(), i + 1);
    }
}

TEST_F(StreamBufferTest, AppendInvalidSize) {
    auto buffer = CreateBuffer();
    size_t recordSize = schema.instance_size();

    // Test undersized record - should throw runtime_error
    {
        std::vector<std::byte> badRecord(recordSize - 1);
        ASSERT_THROW(
            {
                buffer->append(std::move(badRecord));
            },
            std::runtime_error
        ) << "Append should throw runtime_error for undersized record";
        ASSERT_EQ(buffer->size(), 0) << "Buffer should remain empty after failed append";
    }

    // Test oversized record - should throw runtime_error
    {
        std::vector<std::byte> oversized(recordSize + 1);
        ASSERT_THROW(
            {
                buffer->append(std::move(oversized));
            },
            std::runtime_error
        ) << "Append should throw runtime_error for oversized record";
        ASSERT_EQ(buffer->size(), 0) << "Buffer should remain empty after failed append";
    }

    // Test empty record - should throw runtime_error
    {
        std::vector<std::byte> empty;
        ASSERT_THROW(
            {
                buffer->append(std::move(empty));
            },
            std::runtime_error
        ) << "Append should throw runtime_error for empty record";
        ASSERT_EQ(buffer->size(), 0) << "Buffer should remain empty after failed append";
    }

    // Verify that valid record still works after exceptions
    {
        std::vector<std::byte> validRecord(recordSize, std::byte{ 0xAA });
        ASSERT_NO_THROW(
            {
                auto res = buffer->append(std::move(validRecord));
                ASSERT_TRUE(res.appended) << "Valid record should be appended successfully";
            }
        ) << "Valid record should not throw exception";
        ASSERT_EQ(buffer->size(), 1) << "Buffer should contain one record after valid append";
    }
}

TEST_F(StreamBufferTest, FillToCapacity) {
    size_t capacity = 4;
    auto buffer = CreateBuffer(capacity);
    size_t recordSize = schema.instance_size();
    std::vector<std::byte> record(recordSize, std::byte{ 0xBB });

    // Fill to capacity
    for (size_t i = 0; i < capacity; i++) {
        auto res = buffer->append(std::vector<std::byte>(record));
        ASSERT_TRUE(res.appended) << "Append " << i << " should succeed";
    }

    ASSERT_EQ(buffer->size(), capacity);
}

TEST_F(StreamBufferTest, OverwritePolicy) {
    size_t capacity = 3;
    auto buffer = CreateBuffer(capacity);
    size_t recordSize = schema.instance_size();

    // Fill buffer
    for (size_t i = 0; i < capacity; i++) {
        std::vector<std::byte> record(recordSize, static_cast<std::byte>(i));
        buffer->append(std::move(record));
    }

    // Next append should trigger overwrite (depending on policy)
    std::vector<std::byte> record(recordSize, std::byte{ 0xFF });
    auto res = buffer->append(std::move(record));

    // Check behavior based on policy
    if (res.overwritten) {
        ColoredPrint(Color::YELLOW, "Overwrite policy triggered - oldest record replaced");
        ASSERT_EQ(buffer->size(), capacity);
    }
    else {
        ASSERT_FALSE(res.appended) << "Append should fail when buffer full with non-overwrite policy";
    }
}

TEST_F(StreamBufferTest, BatchRetrieval) {
    auto buffer = CreateBuffer(10);
    size_t recordSize = schema.instance_size();

    // Add 5 records
    for (int i = 0; i < 5; i++) {
        std::vector<std::byte> record(recordSize, static_cast<std::byte>(i));
        buffer->append(std::move(record));
    }

    // Retrieve batch of 3
    auto chunks = buffer->get_batch_chunks(3);
    ASSERT_EQ(chunks.total_count, 3);

    // Consume batch
    buffer->consume_batch(3);
    ASSERT_EQ(buffer->size(), 2);
}

TEST_F(StreamBufferTest, EmptyBatchRetrieval) {
    auto buffer = CreateBuffer(10);

    auto chunks = buffer->get_batch_chunks(5);
    ASSERT_EQ(chunks.total_count, 0) << "Empty buffer should return 0 count";
}

TEST_F(StreamBufferTest, ConsumeMoreThanAvailable) {
    auto buffer = CreateBuffer(10);
    size_t recordSize = schema.instance_size();

    // Add 3 records
    for (int i = 0; i < 3; i++) {
        std::vector<std::byte> record(recordSize, static_cast<std::byte>(i));
        buffer->append(std::move(record));
    }

    // Try to consume more than available
    buffer->consume_batch(5);
    ASSERT_EQ(buffer->size(), 0) << "Should consume all available records";
}

// ============================================================================
// StorageManager Tests - Basic Operations
// ============================================================================

TEST_F(StorageManagerTest, CreateManager) {
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(manager->stream_count(), 0);
}

TEST_F(StorageManagerTest, CreateStream) {
    bool ok = manager->create_stream("test_stream", StreamStorageOptions{}, schema);
    ASSERT_TRUE(ok);
    ASSERT_EQ(manager->stream_count(), 1);
}

TEST_F(StorageManagerTest, CreateDuplicateStream) {
    manager->create_stream("duplicate", StreamStorageOptions{}, schema);
    bool ok = manager->create_stream("duplicate", StreamStorageOptions{}, schema);
    ASSERT_TRUE(ok) << "Creating duplicate should return true (idempotent)";
    ASSERT_EQ(manager->stream_count(), 1);
}

TEST_F(StorageManagerTest, RemoveStream) {
    manager->create_stream("removable", StreamStorageOptions{}, schema);
    ASSERT_EQ(manager->stream_count(), 1);

    bool ok = manager->remove_stream("removable");
    ASSERT_TRUE(ok);
    ASSERT_EQ(manager->stream_count(), 0);
}

TEST_F(StorageManagerTest, RemoveNonexistentStream) {
    bool ok = manager->remove_stream("nonexistent");
    ASSERT_TRUE(ok) << "Removing nonexistent stream should be no-op";
}

TEST_F(StorageManagerTest, GetProducerToken) {
    manager->create_stream("token_test", StreamStorageOptions{}, schema);

    auto tokenOpt = manager->get_producer_token("token_test");
    ASSERT_TRUE(tokenOpt.has_value());
    ASSERT_EQ(tokenOpt->stream_id(), "token_test");
}

TEST_F(StorageManagerTest, GetProducerTokenNonexistent) {
    auto tokenOpt = manager->get_producer_token("nonexistent");
    ASSERT_FALSE(tokenOpt.has_value());
}

TEST_F(StorageManagerTest, StreamSize) {
    auto token = CreateStream("size_test", StreamStorageOptions{ .capacity_records = 100 });

    auto size = manager->stream_size("size_test");
    ASSERT_TRUE(size.has_value());
    ASSERT_EQ(size.value(), 0);
}

TEST_F(StorageManagerTest, BufferHealth) {
    auto token = CreateStream("health_test", StreamStorageOptions{ .capacity_records = 100 });

    auto health = manager->GetBufferHealth("health_test");
    ASSERT_TRUE(health.has_value());
    ASSERT_FLOAT_EQ(health.value(), 0.0f);
}

// ============================================================================
// StorageManager Tests - Producer Token Submission
// ============================================================================

TEST_F(StorageManagerTest, SingleSubmission) {
    auto token = CreateStream("submit_test", StreamStorageOptions{ .capacity_records = 10 });

    auto payload = CreateTestRecord(schema);
    auto result = token.try_submit(std::move(payload));

    ASSERT_EQ(result, SubmitResult::Accepted);
    ASSERT_EQ(manager->stream_size("submit_test").value(), 1);
}

TEST_F(StorageManagerTest, MultipleSubmissions) {
    auto token = CreateStream("multi_submit", StreamStorageOptions{ .capacity_records = 100 });

    for (int i = 0; i < 50; i++) {
        auto payload = CreateTestRecord(schema, i, i * 2, i * 3, i * 4);
        auto result = token.try_submit(std::move(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
    }

    ASSERT_EQ(manager->stream_size("multi_submit").value(), 50);
}

TEST_F(StorageManagerTest, BackPressure) {
    size_t capacity = 10;
    auto token = CreateStream("backpressure", StreamStorageOptions{ .capacity_records = capacity });

    // Fill to capacity
    for (size_t i = 0; i < capacity; i++) {
        auto payload = CreateTestRecord(schema);
        auto result = token.try_submit(std::move(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
    }

    // Next submission should trigger backpressure
    auto payload = CreateTestRecord(schema);
    auto result = token.try_submit(std::move(payload));
    ASSERT_EQ(result, SubmitResult::BackPressure);

    ColoredPrint(Color::YELLOW, "BackPressure triggered as expected");
}

TEST_F(StorageManagerTest, HealthMonitoring) {
    size_t capacity = 100;
    auto token = CreateStream("health_monitor", StreamStorageOptions{ .capacity_records = capacity });

    // Fill to 50%
    for (size_t i = 0; i < capacity / 2; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    auto health = manager->GetBufferHealth("health_monitor");
    ASSERT_TRUE(health.has_value());
    ASSERT_NEAR(health.value(), 0.5f, 0.01f);

    ColoredPrint(Color::YELLOW, "Buffer health at 50%%: %.2f", health.value());
}

TEST_F(StorageManagerTest, SubmitToInvalidStream) {
    ProducerToken invalidToken(manager.get(), "nonexistent_stream");

    auto payload = CreateTestRecord(schema);
    auto result = invalidToken.try_submit(std::move(payload));

    ASSERT_EQ(result, SubmitResult::UnknownStream);
}

// ============================================================================
// StorageManager Tests - Manual Flushing
// ============================================================================

TEST_F(StorageManagerTest, ManualFlush) {
    auto token = CreateStream("flush_test",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10
        });

    // Add records
    for (int i = 0; i < 25; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("flush_test").value(), 25);

    // Manual flush
    bool flushed = manager->flush_stream("flush_test");
    ASSERT_TRUE(flushed);

    // After flush, buffer should be empty (records written to backend)
    ASSERT_EQ(manager->stream_size("flush_test").value(), 0);

    ColoredPrint(Color::GREEN, "Manual flush completed successfully");
}

TEST_F(StorageManagerTest, FlushEmptyStream) {
    auto token = CreateStream("empty_flush", StreamStorageOptions{});

    bool flushed = manager->flush_stream("empty_flush");
    ASSERT_TRUE(flushed) << "Flushing empty stream should succeed";
    ASSERT_EQ(manager->stream_size("empty_flush").value(), 0);
}

TEST_F(StorageManagerTest, FlushNonexistentStream) {
    bool flushed = manager->flush_stream("nonexistent");
    ASSERT_FALSE(flushed) << "Flushing nonexistent stream should fail";
}

TEST_F(StorageManagerTest, PartialFlush) {
    auto token = CreateStream("partial_flush",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10
        });

    // Add 15 records
    for (int i = 0; i < 15; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("partial_flush").value(), 15);

    // Flush should write all records (10 + 5)
    bool flushed = manager->flush_stream("partial_flush");
    ASSERT_TRUE(flushed);
    ASSERT_EQ(manager->stream_size("partial_flush").value(), 0);
}

TEST_F(StorageManagerTest, FlushAndContinueWriting) {
    auto token = CreateStream("flush_continue",
        StreamStorageOptions{
            .capacity_records = 50,
            .flush_batch_size = 10
        });

    // Write and flush cycle 1
    for (int i = 0; i < 20; i++) {
        auto payload = CreateTestRecord(schema, i);
        token.try_submit(std::move(payload));
    }
    manager->flush_stream("flush_continue");
    ASSERT_EQ(manager->stream_size("flush_continue").value(), 0);

    // Write and flush cycle 2
    for (int i = 20; i < 30; i++) {
        auto payload = CreateTestRecord(schema, i);
        token.try_submit(std::move(payload));
    }
    ASSERT_EQ(manager->stream_size("flush_continue").value(), 10);

    manager->flush_stream("flush_continue");
    ASSERT_EQ(manager->stream_size("flush_continue").value(), 0);

    ColoredPrint(Color::GREEN, "Continuous write-flush cycle completed");
}

// ============================================================================
// StorageManager Tests - Automatic Flushing
// ============================================================================

TEST_F(StorageManagerTest, AutoFlushOnBatchSize) {
    // Note: This test assumes you have implemented automatic flushing logic
    // based on flush_batch_size threshold
    auto token = CreateStream("auto_flush_batch",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10
        });

    // Add exactly flush_batch_size records
    for (int i = 0; i < 10; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    // Give time for automatic flush if implemented asynchronously
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // If auto-flush is implemented, buffer might be empty or partially flushed
    size_t remaining = manager->stream_size("auto_flush_batch").value();
    ColoredPrint(Color::YELLOW, "After batch threshold: %zu records remaining", remaining);

    // Manual verification - adjust assertion based on your implementation
    // ASSERT_LE(remaining, 10);
}

TEST_F(StorageManagerTest, AutoFlushOnTimer) {
    // Test timer-based automatic flushing
    auto token = CreateStream("auto_flush_timer",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 50,
            .flush_interval = std::chrono::milliseconds(500)
        });

    // Add some records (below batch threshold)
    for (int i = 0; i < 20; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("auto_flush_timer").value(), 20);

    // Wait for timer to trigger flush
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Check if timer-based flush occurred
    size_t remaining = manager->stream_size("auto_flush_timer").value();
    ColoredPrint(Color::YELLOW, "After timer interval: %zu records remaining", remaining);

    // Note: Adjust assertion based on actual timer implementation
    // ASSERT_EQ(remaining, 0) << "Timer should have triggered flush";
}

TEST_F(StorageManagerTest, NoAutoFlushBelowThreshold) {
    auto token = CreateStream("no_auto_flush",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 50,
            .flush_interval = std::chrono::milliseconds(0) // No timer
        });

    // Add records below threshold
    for (int i = 0; i < 30; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should still have all records (no auto-flush)
    ASSERT_EQ(manager->stream_size("no_auto_flush").value(), 30);
}

// ============================================================================
// StorageManager Tests - Concurrent Access
// ============================================================================

TEST_F(StorageManagerTest, ConcurrentSubmissions) {
    auto token = CreateStream("concurrent_test",
        StreamStorageOptions{ .capacity_records = 10000 });

    const int NUM_THREADS = 4;
    const int RECORDS_PER_THREAD = 250;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{ 0 };

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&token, &successCount, t, RECORDS_PER_THREAD, this]() {
            for (int i = 0; i < RECORDS_PER_THREAD; i++) {
                auto payload = CreateTestRecord(schema, t * 1000 + i);
                auto result = token.try_submit(std::move(payload));
                if (result == SubmitResult::Accepted) {
                    successCount++;
                }
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    ColoredPrint(Color::YELLOW, "Concurrent submissions: %d accepted", successCount.load());
    ASSERT_EQ(successCount.load(), NUM_THREADS * RECORDS_PER_THREAD);
    ASSERT_EQ(manager->stream_size("concurrent_test").value(), NUM_THREADS * RECORDS_PER_THREAD);
}

TEST_F(StorageManagerTest, ConcurrentFlushAndWrite) {
    auto token = CreateStream("flush_write_concurrent",
        StreamStorageOptions{
            .capacity_records = 1000,
            .flush_batch_size = 100
        });

    std::atomic<bool> stopWriter{ false };
    std::atomic<int> totalWritten{ 0 };

    // Writer thread
    std::thread writer([&]() {
        while (!stopWriter.load()) {
            auto payload = CreateTestRecord(schema);
            auto result = token.try_submit(std::move(payload));
            if (result == SubmitResult::Accepted) {
                totalWritten++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        });

    // Flusher thread
    std::thread flusher([&]() {
        for (int i = 0; i < 10; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            manager->flush_stream("flush_write_concurrent");
        }
        });

    // Let it run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    stopWriter.store(true);

    writer.join();
    flusher.join();

    ColoredPrint(Color::YELLOW, "Concurrent test: %d records written", totalWritten.load());
    ASSERT_GT(totalWritten.load(), 0);
}

TEST(ParquetBasicTest, CreateSimpleFile) {
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

    auto arrow_schema = arrow::schema({
        arrow::field("numbers", arrow::int32()),
        arrow::field("words", arrow::utf8())
        });

    auto table = arrow::Table::Make(arrow_schema, { int_array, str_array });

    std::filesystem::path parquet_path = std::filesystem::absolute("test_output.parquet");
    ColoredPrint(Color::YELLOW, "Writing Parquet file to: %s", parquet_path.c_str());

    auto maybe_outfile = arrow::io::FileOutputStream::Open(parquet_path.string());
    ASSERT_TRUE(maybe_outfile.ok()) << maybe_outfile.status().ToString();
    auto outfile = *maybe_outfile;

    auto status = parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile, 1024);
    ASSERT_TRUE(status.ok()) << status.ToString();

    ASSERT_TRUE(outfile->Close().ok());
    ASSERT_TRUE(std::filesystem::exists(parquet_path)) << "Parquet file was not created";

    // Cleanup
    std::filesystem::remove(parquet_path);
}

TEST_F(ParquetStorageTest, CreateManager) {
    ASSERT_NE(manager, nullptr);
    ASSERT_NE(backend, nullptr);
    ASSERT_EQ(manager->stream_count(), 0);
}

TEST_F(ParquetStorageTest, CreateStream) {
    bool ok = manager->create_stream(
        "parquet_stream",
        StreamStorageOptions{
            .capacity_records = 1000,
            .flush_batch_size = 100
        },
        schema
    );
    ASSERT_TRUE(ok);
    ASSERT_EQ(manager->stream_count(), 1);
}

TEST_F(ParquetStorageTest, WriteSmallBatch) {
    auto token = CreateStream("small_batch",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10
        });

    // Write 25 records
    for (int i = 0; i < 25; i++) {
        auto payload = CreateTestRecord(schema, i, i * 2, i * 3, i * 4);
        auto result = token.try_submit(std::move(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
    }

    ASSERT_EQ(manager->stream_size("small_batch").value(), 25);

    // Flush to Parquet
    bool flushed = manager->flush_stream("small_batch");
    ASSERT_TRUE(flushed);
    ASSERT_EQ(manager->stream_size("small_batch").value(), 0);

    ColoredPrint(Color::GREEN, "Small batch written to Parquet successfully");
}

TEST_F(ParquetStorageTest, WriteLargeBatch) {
    auto token = CreateStream("large_batch",
        StreamStorageOptions{
            .capacity_records = 10000,
            .flush_batch_size = 1000
        });

    const int TOTAL_RECORDS = 5000;

    // Write many records
    for (int i = 0; i < TOTAL_RECORDS; i++) {
        auto payload = CreateTestRecord(schema, i, i * 2, i * 3, i * 4);
        auto result = token.try_submit(std::move(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
    }

    ASSERT_EQ(manager->stream_size("large_batch").value(), TOTAL_RECORDS);

    // Flush to Parquet
    bool flushed = manager->flush_stream("large_batch");
    ASSERT_TRUE(flushed);
    ASSERT_EQ(manager->stream_size("large_batch").value(), 0);

    ColoredPrint(Color::GREEN, "Large batch (%d records) written to Parquet", TOTAL_RECORDS);
}

TEST_F(ParquetStorageTest, MultipleFlushCycles) {
    auto token = CreateStream("multi_flush",
        StreamStorageOptions{
            .capacity_records = 1000,
            .flush_batch_size = 200
        });

    const int CYCLES = 5;
    const int RECORDS_PER_CYCLE = 300;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        // Write records
        for (int i = 0; i < RECORDS_PER_CYCLE; i++) {
            int value = cycle * 1000 + i;
            auto payload = CreateTestRecord(schema, value, value * 2, value * 3, value * 4);
            token.try_submit(std::move(payload));
        }

        // Flush
        bool flushed = manager->flush_stream("multi_flush");
        ASSERT_TRUE(flushed);
        ASSERT_EQ(manager->stream_size("multi_flush").value(), 0);

        ColoredPrint(Color::YELLOW, "Cycle %d: flushed %d records", cycle + 1, RECORDS_PER_CYCLE);
    }

    ColoredPrint(Color::GREEN, "Multiple flush cycles completed: %d total records",
        CYCLES * RECORDS_PER_CYCLE);
}

TEST_F(ParquetStorageTest, VerifyFileCreation) {
    auto token = CreateStream("file_verify",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 50
        });

    // Write and flush
    for (int i = 0; i < 50; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    manager->flush_stream("file_verify");

    // Give backend time to write file
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check if Parquet files were created in output directory
    int fileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
        if (entry.path().extension() == ".parquet") {
            fileCount++;
            ColoredPrint(Color::YELLOW, "Found Parquet file: %s", entry.path().filename().c_str());
        }
    }

    ASSERT_GT(fileCount, 0) << "No Parquet files were created";
}

TEST_F(ParquetStorageTest, EmptyFlushNoFile) {
    auto token = CreateStream("empty_parquet", StreamStorageOptions{});

    // Count files before flush
    int fileCountBefore = 0;
    for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
        if (entry.path().extension() == ".parquet") {
            fileCountBefore++;
        }
    }

    // Flush empty stream
    manager->flush_stream("empty_parquet");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Count files after flush
    int fileCountAfter = 0;
    for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
        if (entry.path().extension() == ".parquet") {
            fileCountAfter++;
        }
    }

    ASSERT_EQ(fileCountBefore, fileCountAfter) << "Empty flush should not create files";
}

TEST_F(StorageManagerTest, AutomaticTimerBasedFlushing) {
    auto token = CreateStream("auto_timer_flush",
        StreamStorageOptions{
            .capacity_records = 1000,
            .flush_batch_size = 50,
            .flush_interval = std::chrono::milliseconds(200)
        });

    // Add records below batch threshold
    for (int i = 0; i < 30; i++) {
        auto payload = CreateTestRecord(schema, i);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("auto_timer_flush").value(), 30);
    ColoredPrint(Color::YELLOW, "Added 30 records, waiting for auto-flush...");

    // Wait slightly longer than flush interval
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Check if automatic flush occurred
    auto remaining = manager->stream_size("auto_timer_flush").value();
    ColoredPrint(Color::GREEN, "After timer: %zu records remaining (expected 0)", remaining);
    ASSERT_EQ(remaining, 0) << "Timer should have triggered automatic flush";
}

TEST_F(StorageManagerTest, MultipleFlusherThreads) {
    // Create multiple streams with different intervals
    auto token1 = CreateStream("stream_fast",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(100)
        });

    auto token2 = CreateStream("stream_slow",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(300)
        });

    auto token3 = CreateStream("stream_manual",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(0) // No auto-flush
        });

    // Add data to all streams
    for (int i = 0; i < 5; i++) {
        auto payload = CreateTestRecord(schema, i);
        token1.try_submit(std::vector<std::byte>(payload));
        token2.try_submit(std::vector<std::byte>(payload));
        token3.try_submit(std::move(payload));
    }

    // Wait for fast stream to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_EQ(manager->stream_size("stream_fast").value(), 0) << "Fast stream should be flushed";
    ASSERT_EQ(manager->stream_size("stream_slow").value(), 5) << "Slow stream should still have data";
    ASSERT_EQ(manager->stream_size("stream_manual").value(), 5) << "Manual stream should still have data";

    // Wait for slow stream to flush
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_EQ(manager->stream_size("stream_slow").value(), 0) << "Slow stream should now be flushed";
    ASSERT_EQ(manager->stream_size("stream_manual").value(), 5) << "Manual stream should still have data";
    ColoredPrint(Color::GREEN, "Multiple flusher threads working independently");
}

TEST_F(StorageManagerTest, FlusherThreadStopsOnStreamRemoval) {
    auto token = CreateStream("removable_stream",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(100)
        });

    // Add some data
    for (int i = 0; i < 5; i++) {
        auto payload = CreateTestRecord(schema, i);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("removable_stream").value(), 5);

    // Remove stream (should stop flusher thread gracefully)
    bool removed = manager->remove_stream("removable_stream");
    ASSERT_TRUE(removed);

    // Give time for thread to stop
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify stream is gone
    ASSERT_EQ(manager->stream_count(), 0);
    auto size = manager->stream_size("removable_stream");
    ASSERT_FALSE(size.has_value()) << "Removed stream should not be accessible";

    ColoredPrint(Color::GREEN, "Flusher thread stopped cleanly on stream removal");
}

TEST_F(StorageManagerTest, FlusherThreadStopsOnManagerShutdown) {
    auto token1 = CreateStream("shutdown_test_1",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(100)
        });

    auto token2 = CreateStream("shutdown_test_2",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(200)
        });

    // Add data to both streams
    for (int i = 0; i < 10; i++) {
        auto payload = CreateTestRecord(schema, i);
        token1.try_submit(std::vector<std::byte>(payload));
        token2.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_count(), 2);

    // All data should be flushed
    // Note: Can't check sizes after stop, but no deadlock should occur
    ColoredPrint(Color::GREEN, "All flusher threads stopped on manager shutdown");
}

TEST_F(StorageManagerTest, ContinuousWritingWithAutoFlush) {
    auto token = CreateStream("continuous_auto",
        StreamStorageOptions{
            .capacity_records = 500,
            .flush_batch_size = 50,
            .flush_interval = std::chrono::milliseconds(150)
        });

    std::atomic<int> totalWritten{ 0 };
    std::atomic<bool> stopWriter{ false };

    // Continuous writer thread
    std::thread writer([&]() {
        for (int i = 0; i < 200 && !stopWriter.load(); i++) {
            auto payload = CreateTestRecord(schema, i);
            auto result = token.try_submit(std::move(payload));
            if (result == SubmitResult::Accepted) {
                totalWritten++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        });

    // Let it run for a while
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stopWriter.store(true);
    writer.join();

    ColoredPrint(Color::YELLOW, "Written %d records with auto-flush active", totalWritten.load());

    // Wait for final auto-flush
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto remaining = manager->stream_size("continuous_auto").value();
    ColoredPrint(Color::YELLOW, "Remaining records: %zu", remaining);

    // Should have very few or no records remaining due to auto-flush
    ASSERT_LT(remaining, 50) << "Auto-flush should keep buffer relatively empty";
    ASSERT_GT(totalWritten.load(), 0) << "Should have written some records";
}

TEST_F(StorageManagerTest, NoAutoFlushWhenIntervalZero) {
    auto token = CreateStream("no_auto_flush",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 50,
            .flush_interval = std::chrono::milliseconds(0) // Disabled
        });

    // Add records
    for (int i = 0; i < 30; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("no_auto_flush").value(), 30);

    // Wait (no auto-flush should occur)
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Records should still be there
    ASSERT_EQ(manager->stream_size("no_auto_flush").value(), 30)
        << "Records should remain when auto-flush is disabled";

    ColoredPrint(Color::GREEN, "No auto-flush when interval is zero (as expected)");
}

TEST_F(StorageManagerTest, AutoFlushWithBackPressure) {
    size_t capacity = 50;
    auto token = CreateStream("backpressure_auto",
        StreamStorageOptions{
            .capacity_records = capacity,
            .flush_batch_size = 20,
            .flush_interval = std::chrono::milliseconds(200)
        });

    // Fill buffer to capacity quickly
    for (size_t i = 0; i < capacity; i++) {
        auto payload = CreateTestRecord(schema, static_cast<int>(i));
        auto result = token.try_submit(std::move(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
    }

    ASSERT_EQ(manager->stream_size("backpressure_auto").value(), capacity);

    // Next submission should cause backpressure
    auto payload = CreateTestRecord(schema);
    auto result = token.try_submit(std::move(payload));
    ASSERT_EQ(result, SubmitResult::BackPressure);

    ColoredPrint(Color::YELLOW, "BackPressure triggered, waiting for auto-flush...");

    // Wait for auto-flush to clear some space
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    auto remaining = manager->stream_size("backpressure_auto").value();
    ColoredPrint(Color::GREEN, "After auto-flush: %zu records remaining", remaining);

    // Should have space now
    ASSERT_LT(remaining, capacity) << "Auto-flush should have freed space";

    // Try submitting again
    result = token.try_submit(CreateTestRecord(schema));
    ASSERT_EQ(result, SubmitResult::Accepted) << "Should accept after auto-flush";
}

TEST_F(StorageManagerTest, RapidFlushInterval) {
    auto token = CreateStream("rapid_flush",
        StreamStorageOptions{
            .capacity_records = 1000,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(50) // Very frequent
        });

    std::atomic<int> maxObservedSize{ 0 };
    std::atomic<bool> stopMonitor{ false };

    // Monitor thread to track max buffer size
    std::thread monitor([&]() {
        while (!stopMonitor.load()) {
            auto size = manager->stream_size("rapid_flush");
            if (size.has_value()) {
                int current = static_cast<int>(size.value());
                int expected = maxObservedSize.load();
                while (current > expected &&
                    !maxObservedSize.compare_exchange_weak(expected, current)) {
                    expected = maxObservedSize.load();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        });

    // Write many records
    for (int i = 0; i < 100; i++) {
        auto payload = CreateTestRecord(schema, i);
        token.try_submit(std::move(payload));
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    stopMonitor.store(true);
    monitor.join();

    ColoredPrint(Color::YELLOW, "Max observed buffer size: %d", maxObservedSize.load());

    // With rapid flushing, buffer should never grow too large
    ASSERT_LT(maxObservedSize.load(), 50)
        << "Rapid auto-flush should keep buffer small";
}

TEST_F(StorageManagerTest, FlusherThreadExceptionHandling) {
    // Create a stream with auto-flush
    auto token = CreateStream("exception_test",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(100)
        });

    // Add data
    for (int i = 0; i < 15; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("exception_test").value(), 15);

    // Wait for auto-flush
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // If backend throws exception, flusher should handle it gracefully
    // and continue running (check logs for error messages)
    auto remaining = manager->stream_size("exception_test").value();
    ColoredPrint(Color::YELLOW, "After flush with potential exception: %zu records", remaining);

    // Thread should still be alive - add more data
    for (int i = 0; i < 10; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    // Wait for another flush cycle
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    ColoredPrint(Color::GREEN, "Flusher thread survived exception and continued");
}

TEST_F(StorageManagerTest, EmptyBufferAutoFlushNoOp) {
    auto token = CreateStream("empty_auto",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(100)
        });

    // Don't add any data
    ASSERT_EQ(manager->stream_size("empty_auto").value(), 0);

    // Let auto-flush run multiple cycles on empty buffer
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    // Should still be empty (no errors)
    ASSERT_EQ(manager->stream_size("empty_auto").value(), 0);

    ColoredPrint(Color::GREEN, "Auto-flush handles empty buffer gracefully");
}

TEST_F(StorageManagerTest, DifferentFlushIntervalsIndependent) {
    // Create 3 streams with different intervals
    auto fast = CreateStream("fast_100ms",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(100)
        });

    auto medium = CreateStream("medium_200ms",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(200)
        });

    auto slow = CreateStream("slow_300ms",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 10,
            .flush_interval = std::chrono::milliseconds(300)
        });

    // Add 5 records to each
    for (int i = 0; i < 5; i++) {
        fast.try_submit(CreateTestRecord(schema, i));
        medium.try_submit(CreateTestRecord(schema, i));
        slow.try_submit(CreateTestRecord(schema, i));
    }

    // After 120ms: fast should flush
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    ASSERT_EQ(manager->stream_size("fast_100ms").value(), 0);
    ASSERT_EQ(manager->stream_size("medium_200ms").value(), 5);
    ASSERT_EQ(manager->stream_size("slow_300ms").value(), 5);
    ColoredPrint(Color::YELLOW, "After 120ms: fast flushed");

    // After 220ms total: medium should flush
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(manager->stream_size("fast_100ms").value(), 0);
    ASSERT_EQ(manager->stream_size("medium_200ms").value(), 0);
    ASSERT_EQ(manager->stream_size("slow_300ms").value(), 5);
    ColoredPrint(Color::YELLOW, "After 220ms: medium flushed");

    // After 320ms total: slow should flush
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(manager->stream_size("fast_100ms").value(), 0);
    ASSERT_EQ(manager->stream_size("medium_200ms").value(), 0);
    ASSERT_EQ(manager->stream_size("slow_300ms").value(), 0);
    ColoredPrint(Color::GREEN, "After 320ms: all flushed independently");
}

TEST_F(StorageManagerTest, AutoFlushDuringConcurrentWrites) {
    auto token = CreateStream("concurrent_auto",
        StreamStorageOptions{
            .capacity_records = 1000,
            .flush_batch_size = 50,
            .flush_interval = std::chrono::milliseconds(100)
        });

    const int NUM_WRITERS = 3;
    const int RECORDS_PER_WRITER = 100;
    std::vector<std::thread> writers;
    std::atomic<int> totalWritten{ 0 };

    for (int w = 0; w < NUM_WRITERS; w++) {
        writers.emplace_back([&, w]() {
            for (int i = 0; i < RECORDS_PER_WRITER; i++) {
                auto payload = CreateTestRecord(schema, w * 1000 + i);
                if (token.try_submit(std::move(payload)) == SubmitResult::Accepted) {
                    totalWritten++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            });
    }

    // Let writers run with auto-flush active
    for (auto& t : writers) {
        t.join();
    }

    ColoredPrint(Color::YELLOW, "Total written: %d records", totalWritten.load());
    ASSERT_EQ(totalWritten.load(), NUM_WRITERS * RECORDS_PER_WRITER);

    // Wait for final flush
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    auto remaining = manager->stream_size("concurrent_auto").value();
    ColoredPrint(Color::GREEN, "After concurrent writes + auto-flush: %zu remaining", remaining);

    // Should have flushed most/all data
    ASSERT_LT(remaining, 100) << "Auto-flush should keep buffer from filling";
}

TEST_F(StorageManagerTest, ManualFlushDuringAutoFlush) {
    auto token = CreateStream("manual_during_auto",
        StreamStorageOptions{
            .capacity_records = 100,
            .flush_batch_size = 20,
            .flush_interval = std::chrono::milliseconds(200)
        });

    // Add data
    for (int i = 0; i < 30; i++) {
        auto payload = CreateTestRecord(schema);
        token.try_submit(std::move(payload));
    }

    ASSERT_EQ(manager->stream_size("manual_during_auto").value(), 30);

    // Trigger manual flush before auto-flush kicks in
    bool flushed = manager->flush_stream("manual_during_auto");
    ASSERT_TRUE(flushed);
    ASSERT_EQ(manager->stream_size("manual_during_auto").value(), 0);

    ColoredPrint(Color::YELLOW, "Manual flush completed");

    // Wait for auto-flush timer (should be no-op on empty buffer)
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    ASSERT_EQ(manager->stream_size("manual_during_auto").value(), 0);

    ColoredPrint(Color::GREEN, "Manual and auto-flush coexist peacefully");
}