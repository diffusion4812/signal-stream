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
std::vector<std::byte> CreateTestRecord(const Schema& schema, int32_t val1 = 12345, int32_t val2 = 24680, int64_t val3 = 54321, int64_t val4 = 13579) {
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
    Schema schema_;

    void SetUp() override {
        schema_.add_field("field1", Kind::Int32);
        schema_.add_field("field2", Kind::Int32);
        schema_.add_field("field3", Kind::Int64);
        schema_.add_field("field4", Kind::Int64);
        schema_.finalize();
    }
};

class CSVBackendTest : public SchemaTest {
protected:
    std::unique_ptr<StorageManager> manager_;

    void SetUp() override {
        SchemaTest::SetUp();
        manager_ = std::make_unique<StorageManager>();
    }
};

// ============================================================================
// Backend Tests
// ============================================================================

TEST_F(CSVBackendTest, CreateStream) {
    bool ok = manager_->create_stream(
        "CSV Stream",
        StreamStorageOptions{
            .capacity_records = 1'000'000,
            .flush_batch_size = 50'000,
            .flush_interval { 50 },
            .backend_config = ParquetBackend::Config {
                .rotation = FileRotationConfig {
                    .max_records_per_file = 50000,
					.max_file_size_bytes = 100 * 1024, // 100 kB
                    .max_file_duration { 60'000 },
                    .output_directory = "./data",
                    .filename_pattern = "{stream}_{timestamp}_{sequence}"  //
                    }
            }
        },
        schema_
    );
    ASSERT_TRUE(ok);
    ASSERT_EQ(manager_->stream_count(), 1); // Verify stream has been created

    ProducerToken token = manager_->get_producer_token("CSV Stream").value();

    for (int i = 0; i < 50000; i++) {
        auto payload = CreateTestRecord(schema_, i, i * 2, i * 3, i * 4);
        auto result = token.try_submit(std::move(payload));
        ASSERT_EQ(result, SubmitResult::Accepted);
		//std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    }

	//std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    //ColoredPrint(Color::YELLOW, "buffer health: %.2f records flushed to disk: %2d", manager_->GetBufferHealth("CSV Stream").value(), manager_->get_backend_records("CSV Stream").value());
}