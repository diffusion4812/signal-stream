#include <chrono>
#include <thread>
#include <cstdarg>

#include <gtest/gtest.h>

#include "schema.h"
#include "instance.h"
#include "storage-buffer.h"
#include "storage-manager.h"

Schema schema;
StreamBuffer* buff;
StorageManager* man;

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
    // Simple use of vprintf to stdout (or format into std::string with vsnprintf)
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

    ColoredPrint(Color::YELLOW, "             instance size = %d\n", schema.instance_size());
}

TEST(Storage, CreateBuffer) {
    buff = new StreamBuffer(1024, schema.instance_size());
    ASSERT_TRUE(buff);
}

TEST(Storage, AppendBytes) {
    Instance instance(schema);
    instance.set<int32_t>("field1", 12345);
    instance.set<int32_t>("field2", 24680);
    instance.set<int64_t>("field3", 54321);
    instance.set<int64_t>("field4", 13579);
    for (auto i = 0; i < 100; i++)
        buff->append_batch(reinterpret_cast<uint8_t*>(instance.get_data()), schema.instance_size());
}

TEST(Storage, GetSize) {
    ColoredPrint(Color::YELLOW, "             buffer size = %d\n", buff->size());
}

TEST(storage, GetLatestRecords) {

    std::vector<uint8_t> records;
    records = buff->latest(30);

    ColoredPrint(Color::YELLOW, "             records vector size = %d\n", records.size());
    ColoredPrint(Color::YELLOW, "             buffer size = %d\n", records.size() / buff->record_size());

    ASSERT_EQ(records.size() / buff->record_size(), 30);
}

TEST(Manager, CreateManager) {
    man = new StorageManager();
    ASSERT_TRUE(man);
}

TEST(Manager, AddStream) {
    bool ok = man->create_stream("teststream", StreamOptions{
        .capacity = 1024 * 1024,
        .flush_batch_size = 0,
        .flush_interval = std::chrono::milliseconds(0)
        }, schema.instance_size());
    ASSERT_TRUE(ok);
}

TEST(Manager, AppendToStream) {
    Instance instance(schema);
    instance.set<int32_t>("field1", 12345);
    instance.set<int32_t>("field2", 24680);
    instance.set<int64_t>("field3", 54321);
    instance.set<int64_t>("field4", 13579);
    for (auto i = 0; i < 1600; i++)
        man->append_batch_bytes("teststream",
            std::vector<uint8_t>(reinterpret_cast<uint8_t*>(instance.get_data()),
                reinterpret_cast<uint8_t*>(instance.get_data()) + schema.instance_size())
        );
    auto records = man->stream_size("teststream").value();
    ColoredPrint(Color::YELLOW, "             teststream records stored = %d\n", records);
    ASSERT_EQ(records, 1600);
}