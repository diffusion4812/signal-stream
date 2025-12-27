#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "service-source-registry.h"
#include "storage-buffer.h"

// timestamp alias
using ts_t = std::int64_t;

// Abstract persistence backend
struct IStorageBackend {
    virtual ~IStorageBackend() = default;
    virtual bool write_batch_two_pass(const std::string& streamId, const StreamBuffer::BatchChunks& chunks) = 0;
};

#include "service-storage-backend-null.h"
#include "service-storage-backend-parquet.h"

// Stream configuration options
struct StreamStorageOptions {
    // capacity expressed in number of records for the buffer backing the stream
    size_t capacity_records = 1000;
    // flush batch size expressed in number of records
    size_t flush_batch_size = 1;
    // timer interval used by timer_loop (not strictly required if you set 0)
    std::chrono::milliseconds flush_interval{ 0 };
};

// Result codes for submit
enum class SubmitResult { Accepted, BackPressure, InvalidPayloadSize, UnknownStream };

// Forward
class StorageManager;

// Lightweight producer handle returned to services.
// Small wrapper that references the manager and target streamId.
// All methods are thread-safe for typical use (delegates into manager).
class ProducerToken {
public:
    ProducerToken() = default;
    ProducerToken(StorageManager* mgr, std::string id);

    // Non-blocking submit: attempts to enqueue the batch for async persistence.
    SubmitResult try_submit(std::vector<std::byte>&& batch) const;

    const std::string& stream_id() const { return streamId_; }

private:
    StorageManager* mgr_ = nullptr;
    std::string streamId_;
};

struct StreamBufferHandle {
    StreamBuffer* buf;

    explicit StreamBufferHandle(StreamBuffer* b) noexcept;

    // Move constructor
    StreamBufferHandle(StreamBufferHandle&& other) noexcept;

    // Move assignment
    StreamBufferHandle& operator=(StreamBufferHandle&& other) noexcept;

    // Delete copy operations
    StreamBufferHandle(const StreamBufferHandle&) = delete;
    StreamBufferHandle& operator=(const StreamBufferHandle&) = delete;

    StreamBuffer* get() const noexcept { return buf; }
    explicit operator bool() const noexcept { return buf != nullptr; }
};

class StorageManager {
public:
    explicit StorageManager();
    ~StorageManager();

    bool create_stream(const std::string& streamId,
        const StreamStorageOptions& opts,
        const Schema& s);

    bool remove_stream(const std::string& streamId);

    // Acquire a token for a stream (cheap handle)
    std::optional<ProducerToken> get_producer_token(const std::string& streamId);

    SubmitResult submit_batch_for_stream(const std::string& streamId, std::vector<std::byte>&& userPayload);

    // Persistence control
    bool flush_stream(const std::string& streamId);

    size_t stream_count() const;

    std::optional<size_t> stream_size(const std::string& servicename) const;

    std::optional<StreamBufferHandle> GetBufferHandle(const std::string& streamId);

    std::optional<float> GetBufferHealth(const std::string& servicename) const;

private:
    struct StorageStreamHolder {
        std::string stream_name;
        std::unique_ptr<StreamBuffer> buffer;
        StreamStorageOptions opts;
        std::jthread flusher_thread;
        std::atomic<bool> stop_flusher{ false };
        std::mutex flusher_mtx;
		std::unique_ptr<IStorageBackend> backend; // owned backend instance
        std::condition_variable flusher_cv;
        size_t flush_batch_size;

        StorageStreamHolder(
            std::unique_ptr<StreamBuffer> buf,
            StreamStorageOptions o,
            std::string name,
            std::unique_ptr<IStorageBackend> backend
        );
        ~StorageStreamHolder();

        StorageStreamHolder(const StorageStreamHolder&) = delete;
        StorageStreamHolder& operator=(const StorageStreamHolder&) = delete;
        StorageStreamHolder(StorageStreamHolder&&) = delete;
        StorageStreamHolder& operator=(StorageStreamHolder&&) = delete;
    };

    StorageStreamHolder* get_holder(const std::string& streamId) const;
    void flusher_thread_func(const std::string& streamId, StorageStreamHolder* holder, std::chrono::milliseconds interval);

    std::atomic<bool> stopFlag_;
    bool running_;
    mutable std::mutex streamsMtx_;
    std::unordered_map<std::string, std::unique_ptr<StorageStreamHolder>> streams_; // per-stream buffers and options
    std::mutex lifecycleMtx_;
    std::condition_variable cv_;

    friend class ProducerToken;
};