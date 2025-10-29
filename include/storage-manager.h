#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <deque>
#include <optional>

#include "stream-registry.h"
#include "storage-buffer.h"

// timestamp alias
using ts_t = std::int64_t;

// Abstract persistence backend
struct StorageBackend {
    virtual ~StorageBackend() = default;
    virtual bool write_batch(const std::string& streamId, const std::vector<std::byte>& batch) = 0;
};

// Null backend (no-op)
struct NullBackend : public StorageBackend {
    bool write_batch(const std::string& streamId, const std::vector<std::byte>& batch) {
        return true;
    }
};

// Stream configuration options
struct StreamStorageOptions {
    // capacity expressed in number of bytes for the ring buffer backing this stream
    size_t capacity = 1 * 8 * 1024;
    // flush batch size expressed in number of records
    size_t flush_batch_size = 128;
    // timer interval used by timer_loop (not strictly required if you set 0)
    std::chrono::milliseconds flush_interval{ 1000 };
};

struct StorageStreamHolder {
    std::shared_ptr<StreamBuffer> buffer; // shared ownership for consumers
    StreamOptions opts;
    size_t recordSizeBytes;
    StreamMetadata metadata;

    StorageStreamHolder(std::shared_ptr<StreamBuffer> buf, StreamOptions o, size_t rs)
        : buffer(std::move(buf)), opts(std::move(o)), recordSizeBytes(rs) {
    }

    StorageStreamHolder(const StorageStreamHolder&) = delete;
    StorageStreamHolder& operator=(const StorageStreamHolder&) = delete;
    StorageStreamHolder(StorageStreamHolder&&) = delete;
    StorageStreamHolder& operator=(StorageStreamHolder&&) = delete;
};

// Result codes for submit
enum class SubmitResult { Accepted, Backpressure, InvalidPayloadSize, UnknownStream };

// Forward
class StorageManager;

// Lightweight producer handle returned to services.
// Small wrapper that references the manager and target streamId.
// All methods are thread-safe for typical use (delegates into manager).
class ProducerToken {
public:
    ProducerToken() = default;
    ProducerToken(StorageManager* mgr, std::string id) : mgr_(mgr), streamId_(std::move(id)) {}

    // Non-blocking submit: attempts to enqueue the batch for async persistence.
    SubmitResult try_submit(std::vector<std::byte> && batch) const;

    const std::string& stream_id() const { return streamId_; }

private:
    StorageManager* mgr_ = nullptr;
    std::string streamId_;
};

struct StreamBufferHandle {
    StreamBuffer* buf;

    explicit StreamBufferHandle(StreamBuffer* b) noexcept : buf(b) {}

    // Move constructor
    StreamBufferHandle(StreamBufferHandle&& other) noexcept
        : buf(other.buf) {
        other.buf = nullptr;
    }

    // Move assignment
    StreamBufferHandle& operator=(StreamBufferHandle&& other) noexcept {
        if (this != &other) {
            buf = other.buf;
            other.buf = nullptr;
        }
        return *this;
    }

    // Delete copy operations
    StreamBufferHandle(const StreamBufferHandle&) = delete;
    StreamBufferHandle& operator=(const StreamBufferHandle&) = delete;

    StreamBuffer* get() const noexcept { return buf; }
    explicit operator bool() const noexcept { return buf != nullptr; }
};

class StorageManager {
public:
    // ctor/dtor
    explicit StorageManager(std::shared_ptr<StorageBackend> backend = std::make_shared<NullBackend>(),
        size_t flusherThreads = 1)
        : backend_(std::move(backend)),
        stopFlag_(false),
        flusherThreads_(flusherThreads),
        running_(false) {
    }

    ~StorageManager() {
        stop();
    }

    // Lifecycle
    void start() {
        std::scoped_lock<std::mutex> lk(lifecycleMtx_);
        if (running_) return;
        stopFlag_.store(false);
        running_ = true;
    }

    void stop(bool flush_all = true) {
        {
            std::scoped_lock<std::mutex> lk(lifecycleMtx_);
            if (!running_) return;
            stopFlag_.store(true);
            cv_.notify_all();
        }

        if (timerThread_.joinable()) timerThread_.join();
        for (auto& t : flushers_) {
            if (t.joinable()) t.join();
        }
        flushers_.clear();

        if (flush_all) {
            force_flush_all();
        }

        running_ = false;
    }

    void handle_registry_event(RegistryEvent::Type type, const std::string& streamId, const StreamMetadata& meta) {
        if (type == RegistryEvent::Type::Created) {
            StreamStorageOptions opts;
            opts.capacity = 8 * 1024 * 1024; // Example: 8 KB buffer
            opts.flush_batch_size = 128;
            opts.flush_interval = std::chrono::milliseconds(1000);

            size_t userRecordSize = meta.schema.instance_size(); // Get user record size directly from the schema
            create_stream(streamId, opts, userRecordSize);
        }
        else if (type == RegistryEvent::Type::Deleted) {
            remove_stream(streamId);
        }
        else if (type == RegistryEvent::Type::Updated) {
            // Optional: handle changes to stream options
            // Could resize buffer, adjust flush settings, etc.
        }
        else if (type == RegistryEvent::Type::Renamed) {
            // Optional: handle rename logic if needed
        }
    }

    bool create_stream(const std::string& streamId, const StreamStorageOptions& opts, size_t userRecordSize) {
        constexpr size_t ts_bytes = sizeof(ts_t);
        size_t recordSize = ts_bytes + userRecordSize;

        std::scoped_lock<std::mutex> lk(streamsMtx_);
        if (streams_.contains(streamId)) {
            return true; // already exists: no-op
        }
        auto buf = std::make_unique<StreamBuffer>(opts.capacity, recordSize);
        streams_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(streamId),
            std::forward_as_tuple(std::move(buf), opts, recordSize)
        );
        return true;
    }

    bool remove_stream(const std::string& streamId) {
        std::scoped_lock<std::mutex> lk(streamsMtx_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) {
            return true; // already removed: no-op
        }
        force_flush(streamId);
        streams_.erase(it);
        return true;
    }

    // Acquire a token for a stream (cheap handle)
    std::optional<ProducerToken> get_producer_token(const std::string& streamId) {
        std::scoped_lock<std::mutex> lk(streamsMtx_);
        if (!streams_.count(streamId)) return std::nullopt;
        return ProducerToken(this, streamId);
    }

    SubmitResult submit_batch_for_stream(const std::string& streamId, std::vector<std::byte>&& userPayload) {
        StorageStreamHolder* holder = get_holder(streamId);
        if (!holder) return SubmitResult::UnknownStream;

        size_t recordSize = holder->buffer->record_size();
        constexpr size_t ts_bytes = sizeof(ts_t);
        size_t userRecordSize = recordSize - ts_bytes;

        if (userPayload.size() != userRecordSize) { // TODO: Allow submission of batches
            return SubmitResult::InvalidPayloadSize;
        }

        // Build record with timestamp prefix
        std::vector<std::byte> record;
        record.resize(recordSize);
        
        // Compute and add timestamp
        ts_t ts = currentTimeNs();
        std::memcpy(record.data(), &ts, ts_bytes);

        // copy payload after timestamp prefix
        std::memcpy(record.data() + ts_bytes, userPayload.data(), userRecordSize);

        // move into StreamBuffer
        size_t appended = holder->buffer->append_batch(std::move(record));
        if (appended == 0) {
            return SubmitResult::Backpressure;
        }

        return SubmitResult::Accepted;
    }

    // Reader APIs (for UI / archiver)
    // latest returns contiguous bytes, newest-first (each record is recordSizeBytes)
    std::vector<std::byte> get_latest_bytes(const std::string& streamId, size_t n) const {
        StorageStreamHolder* holder = get_holder(streamId);
        if (!holder) return {};
        return holder->buffer->latest(n);
    }

    // Query by timestamp requires including timestamp in record header; this API returns raw records matching predicate if supported.
    std::vector<std::byte> query_range_bytes(const std::string& streamId, ts_t fromTs, ts_t toTs) const {
        StorageStreamHolder* holder = get_holder(streamId);
        if (!holder) return {};
        // The buffer supports a predicate-based query; caller must ensure record layout
        auto predicate = [fromTs, toTs](const std::byte* rec) -> bool {
            // Placeholder: real predicate depends on record format.
            // For safety, return true (client should call query_if with a custom predicate if needed).
            (void)rec;
            return true;
            };
        return holder->buffer->query_if(predicate);
    }

    // Persistence control
    bool force_flush(const std::string& streamId, std::chrono::milliseconds /*timeout*/ = std::chrono::milliseconds(5000)) {
        StorageStreamHolder* holder = get_holder(streamId);
        if (!holder) return false;
        while (true) {
            auto batch = holder->buffer->take_oldest_batch(holder->opts.flush_batch_size);
            if (batch.empty()) break;
            if (!backend_->write_batch(streamId, batch)) {
                return false;
            }
        }
        return true;
    }

    bool force_flush_all(std::chrono::milliseconds /*timeout*/ = std::chrono::milliseconds(10000)) {
        std::vector<std::string> ids;
        {
            std::lock_guard<std::mutex> lk(streamsMtx_);
            ids.reserve(streams_.size());
            for (const auto& kv : streams_) ids.push_back(kv.first);
        }
        bool ok = true;
        for (const auto& id : ids) {
            if (!force_flush(id)) ok = false;
        }
        return ok;
    }

    // Observability
    size_t stream_count() const {
        std::scoped_lock lk(streamsMtx_);
        return streams_.size();
    }

    std::optional<size_t> stream_size(const std::string& servicename) const {
        StorageStreamHolder* holder = get_holder(servicename);
        if (!holder) return std::nullopt;
        return holder->buffer->size();
    }

    std::optional<StreamBufferHandle> GetBufferHandle(const std::string& streamId) {
        std::unique_lock lk(streamsMtx_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) {
            return std::nullopt;
        }
        // Lock is released here — caller must lock if needed
        return StreamBufferHandle{ it->second.buffer.get() };
    }

    std::optional<float> GetBufferHealth(const std::string& servicename) const {
        StorageStreamHolder* holder = get_holder(servicename);
        if (!holder) return std::nullopt;
        return static_cast<float>(holder->buffer->size()) / holder->buffer->capacity_records();
    }

private:
    struct StorageStreamHolder {
        std::unique_ptr<StreamBuffer> buffer; // byte-based ring buffer
        StreamStorageOptions opts;
        size_t recordSizeBytes;
        std::atomic<uint64_t> nextSeq{ 0 }; // per-stream monotonic seq (optional)

        // Explicit constructor to allow in-place construction inside unordered_map
        StorageStreamHolder(std::unique_ptr<StreamBuffer> buf, StreamStorageOptions o, size_t rs)
            : buffer(std::move(buf)), opts(std::move(o)), recordSizeBytes(rs), nextSeq(0) {
        }
    };

    struct BatchItem {
        std::string streamId;
        std::vector<std::byte> batch; // contiguous records
    };

    StorageStreamHolder* get_holder(const std::string& streamId) const {
        std::scoped_lock<std::mutex> lk(streamsMtx_);
        auto it = streams_.find(streamId);
        return (it == streams_.end()) ? nullptr : const_cast<StorageStreamHolder*>(&it->second);
    }

    // Enqueue batch for background flush
    void enqueue_batch(const std::string& streamId, std::vector<std::byte>&& batch) {
        {
            std::scoped_lock<std::mutex> lk(queueMtx_);
            batchQueue_.emplace_back(BatchItem{ streamId, std::move(batch) });
        }
        cv_.notify_one();
    }

    static ts_t currentTimeNs() {
        return static_cast<ts_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    std::shared_ptr<StorageBackend> backend_;
    size_t queueCapacity_;
    std::atomic<bool> stopFlag_;
    size_t flusherThreads_;
    bool running_ = false;
    mutable std::mutex streamsMtx_;
    std::unordered_map<std::string, StorageStreamHolder> streams_; // per-stream buffers and options
    std::mutex queueMtx_;
    std::deque<BatchItem> batchQueue_; // batches awaiting flush
    std::vector<std::thread> flushers_; // background flusher threads
    std::thread timerThread_; // optional timer to trigger small flushes
    std::mutex lifecycleMtx_;
    std::condition_variable cv_;

    // config
    size_t per_stream_size_limit_ = 10 * 1024 * 1024; // 10 MB
    std::chrono::milliseconds max_buffer_age_ = std::chrono::minutes(3);
    std::chrono::milliseconds flusher_interval_ = std::chrono::seconds(1);

    // buffers and sync
    std::unordered_map<std::string, StreamBuffer> buffers_;
    std::mutex buffersMtx_;
    std::atomic<size_t> global_buffered_bytes_{ 0 };

    // flusher
    std::condition_variable flusherCv_;
    std::mutex flusherMtx_;
    std::thread flusherThread_;
    std::atomic<bool> stopFlusher_{ false };

    friend class ProducerToken;
};