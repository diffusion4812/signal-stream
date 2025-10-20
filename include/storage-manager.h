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

#include "storage-buffer.h"

// timestamp alias
using ts_t = std::int64_t;

// Abstract persistence backend
struct StorageBackend {
    virtual ~StorageBackend() = default;
    virtual bool write_batch(const std::string& streamId, const std::vector<uint8_t>& batch) = 0;
};

// Null backend (no-op)
struct NullBackend : public StorageBackend {
    bool write_batch(const std::string& streamId, const std::vector<uint8_t>& batch) {
        return true;
    }
};

// Stream configuration options
struct StreamOptions {
    // capacity expressed in number of bytes for the ring buffer backing this stream
    size_t capacity = 4 * 1024 * 1024;
    // flush batch size expressed in number of records
    size_t flush_batch_size = 128;
    // timer interval used by timer_loop (not strictly required if you set 0)
    std::chrono::milliseconds flush_interval{ 1000 };
};

// Result codes for submit
enum class SubmitResult { Accepted, Backpressure, UnknownStream };

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
    SubmitResult try_submit(std::vector<uint8_t>&& batch) const;

    const std::string& stream_id() const { return streamId_; }

private:
    StorageManager* mgr_ = nullptr;
    std::string streamId_;
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
        std::lock_guard<std::mutex> lk(lifecycleMtx_);
        if (running_) return;
        stopFlag_.store(false);
        for (size_t i = 0; i < flusherThreads_; ++i) {
            flushers_.emplace_back(&StorageManager::flusher_loop, this);
        }
        //timerThread_ = std::thread(&StorageManager::timer_loop, this);
        running_ = true;
    }

    void stop(bool flush_all = true) {
        {
            std::lock_guard<std::mutex> lk(lifecycleMtx_);
            if (!running_) return;
            stopFlag_.store(true);
            cv_.notify_all();
        }

        if (timerThread_.joinable()) timerThread_.join();
        for (auto& t : flushers_) {
            if (t.joinable()) t.join();
        }
        flushers_.clear();

        // drain queue synchronously
        drain_queue_on_shutdown();

        if (flush_all) {
            force_flush_all();
        }

        running_ = false;
    }

    // Stream lifecycle (ProjectManager calls these)
    // recordSizeBytes: size of each record for this stream (fixed)
    bool create_stream(const std::string& streamId, const StreamOptions& opts, size_t recordSizeBytes) {
        std::lock_guard<std::mutex> lk(streamsMtx_);
        if (streams_.count(streamId)) return false;

        auto buf = std::make_unique<StreamBuffer>(opts.capacity, recordSizeBytes);

        // Construct the pair in-place: key from streamId, mapped value from (buf, opts, recordSizeBytes)
        auto res = streams_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(streamId),
            std::forward_as_tuple(std::move(buf), opts, recordSizeBytes)
        );

        return res.second;
    }

    bool remove_stream(const std::string& streamId) {
        force_flush(streamId);
        std::lock_guard<std::mutex> lk(streamsMtx_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) return false;
        streams_.erase(it);
        return true;
    }

    // Acquire a token for a stream (cheap handle)
    std::optional<ProducerToken> get_producer_token(const std::string& streamId) {
        std::lock_guard<std::mutex> lk(streamsMtx_);
        if (!streams_.count(streamId)) return std::nullopt;
        return ProducerToken(this, streamId);
    }

    // Called by ProducerToken
    SubmitResult submit_batch_for_stream(const std::string& streamId, std::vector<uint8_t>&& batch) {
        // Verify stream exists
        {
            std::lock_guard<std::mutex> lk(streamsMtx_);
            if (!streams_.count(streamId)) return SubmitResult::UnknownStream;
        }

        std::lock_guard<std::mutex> qlk(queueMtx_);
        if (batchQueue_.size() >= queueCapacity_) {
            // Backpressure hint to producer: queue full
            return SubmitResult::Backpressure;
        }

        batchQueue_.emplace_back(streamId, std::move(batch));
        cv_.notify_one();
        return SubmitResult::Accepted;
    }

    // Writer APIs - accept already-serialized record bytes
    // data must point to recordSizeBytes for append_bytes
    bool append_bytes(const std::string& streamId, const uint8_t* data, size_t len) {
        StreamHolder* holder = get_holder(streamId);
        if (!holder) return false;
        if (len != holder->recordSizeBytes) return false;
        bool ok = holder->buffer->append(data, len);
        if (!ok) return false;
        if (holder->buffer->size() >= holder->opts.flush_batch_size) {
            auto batch = holder->buffer->take_oldest_batch(holder->opts.flush_batch_size);
            enqueue_batch(streamId, std::move(batch));
        }
        return true;
    }

    // Batch: contiguous records (len must be multiple of recordSizeBytes)
    bool append_batch_bytes(const std::string& streamId, const std::vector<uint8_t>& batch) {
        StreamHolder* holder = get_holder(streamId);
        if (!holder) return false;
        if (batch.empty()) return true;
        if (batch.size() % holder->recordSizeBytes != 0) return false;
        size_t appended = holder->buffer->append_batch(batch.data(), batch.size());
        if (holder->buffer->size() >= holder->opts.flush_batch_size) {
            auto b = holder->buffer->take_oldest_batch(holder->opts.flush_batch_size);
            enqueue_batch(streamId, std::move(b));
        }
        return appended > 0;
    }

    // Reader APIs (for UI / archiver)
    // latest returns contiguous bytes, newest-first (each record is recordSizeBytes)
    std::vector<uint8_t> get_latest_bytes(const std::string& streamId, size_t n) const {
        StreamHolder* holder = get_holder(streamId);
        if (!holder) return {};
        return holder->buffer->latest(n);
    }

    // Query by timestamp requires including timestamp in record header; this API returns raw records matching predicate if supported.
    std::vector<uint8_t> query_range_bytes(const std::string& streamId, ts_t fromTs, ts_t toTs) const {
        StreamHolder* holder = get_holder(streamId);
        if (!holder) return {};
        // The buffer supports a predicate-based query; caller must ensure record layout
        auto predicate = [fromTs, toTs](const uint8_t* rec) -> bool {
            // Placeholder: real predicate depends on record format.
            // For safety, return true (client should call query_if with a custom predicate if needed).
            (void)rec;
            return true;
            };
        return holder->buffer->query_if(predicate);
    }

    // Persistence control
    bool force_flush(const std::string& streamId, std::chrono::milliseconds /*timeout*/ = std::chrono::milliseconds(5000)) {
        StreamHolder* holder = get_holder(streamId);
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
        std::lock_guard<std::mutex> lk(streamsMtx_);
        return streams_.size();
    }

    std::optional<size_t> stream_size(const std::string& streamId) const {
        StreamHolder* holder = get_holder(streamId);
        if (!holder) return std::nullopt;
        return holder->buffer->size();
    }

private:
    struct StreamHolder {
        std::unique_ptr<StreamBuffer> buffer; // byte-based ring buffer
        StreamOptions opts;
        size_t recordSizeBytes;
        std::atomic<uint64_t> nextSeq{ 0 }; // per-stream monotonic seq (optional)

        // Explicit constructor to allow in-place construction inside unordered_map
        StreamHolder(std::unique_ptr<StreamBuffer> buf, StreamOptions o, size_t rs)
            : buffer(std::move(buf)), opts(std::move(o)), recordSizeBytes(rs), nextSeq(0) {
        }
        // delete copy to be explicit (optional)
        StreamHolder(const StreamHolder&) = delete;
        StreamHolder& operator=(const StreamHolder&) = delete;
        // Allow default move if members permit (std::atomic prevents implicit move),
        // so we keep move deleted as well to be explicit:
        StreamHolder(StreamHolder&&) = delete;
        StreamHolder& operator=(StreamHolder&&) = delete;
    };

    struct BatchItem {
        std::string streamId;
        std::vector<uint8_t> batch; // contiguous records
    };

    StreamHolder* get_holder(const std::string& streamId) const {
        std::lock_guard<std::mutex> lk(streamsMtx_);
        auto it = streams_.find(streamId);
        return (it == streams_.end()) ? nullptr : const_cast<StreamHolder*>(&it->second);
    }

    // Enqueue batch for background flush
    void enqueue_batch(const std::string& streamId, std::vector<uint8_t>&& batch) {
        {
            std::lock_guard<std::mutex> lk(queueMtx_);
            batchQueue_.emplace_back(BatchItem{ streamId, std::move(batch) });
        }
        cv_.notify_one();
    }

    // Timer thread wakes flushers periodically to flush small batches
    void timer_loop() {
        while (!stopFlag_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::vector<std::pair<std::string, std::vector<uint8_t>>> toEnqueue;
            {
                std::lock_guard<std::mutex> lk(streamsMtx_);
                for (auto& kv : streams_) {
                    auto& id = kv.first;
                    auto& holder = kv.second;
                    size_t sz = holder.buffer->size();
                    if (sz > 0 && sz < holder.opts.flush_batch_size && holder.opts.flush_batch_size > 0) {
                        auto batch = holder.buffer->take_oldest_batch(sz);
                        if (!batch.empty()) toEnqueue.emplace_back(id, std::move(batch));
                    }
                }
            }
            for (auto& p : toEnqueue) enqueue_batch(p.first, std::move(p.second));
            cv_.notify_all();
        }
    }

    void flusher_loop() {
        while (true) {
            BatchItem item;
            {
                std::unique_lock<std::mutex> lk(queueMtx_);
                cv_.wait(lk, [this] { return stopFlag_.load() || !batchQueue_.empty(); });

                // If stop requested and queue empty, exit loop.
                if (stopFlag_.load() && batchQueue_.empty()) return;

                if (batchQueue_.empty()) {
                    // Spurious wake or stopFlag set but items taken by another flusher; continue.
                    continue;
                }

                item = std::move(batchQueue_.front());
                batchQueue_.pop_front();
            }

            // Process the batch (with retries/backoff)
            try {
                flush_front_batch(std::move(item));
            }
            catch (const std::exception& ex) {
                std::cerr << "flusher_loop: unexpected exception: " << ex.what() << "";
            }
        }
    }

    // internal flush helpers
    void flush_front_batch(BatchItem&& item) {
        const int maxRetries = 5;
        const std::chrono::milliseconds baseBackoff(50);

        for (int attempt = 1; attempt <= maxRetries; ++attempt) {
            bool ok = false;
            try {
                ok = backend_->write_batch(item.streamId, item.batch);
            }
            catch (const std::exception& ex) {
                ok = false;
                std::cerr << "flush_front_batch: exception writing batch for stream " << item.streamId << ": " << ex.what() << " (attempt " << attempt << ")";
            }

            if (ok) {
                return;
            }

            std::cerr << "flush_front_batch: failed to write batch for stream " << item.streamId << " (attempt " << attempt << ")";

            if (attempt < maxRetries) {
                auto backoff = baseBackoff * (1u << (attempt - 1));
                std::this_thread::sleep_for(backoff);
            }
        }

        // All retries exhausted — decision: drop batch and log error.
        // Alternative: push to a dead-letter queue or persist locally for later replay.
        std::cerr << "flush_front_batch: dropping batch for stream " << item.streamId << " after max retries";
    }

    void drain_queue_on_shutdown() {
        std::deque<BatchItem> localQueue;
        {
            // Move queue contents out under lock to minimize blocking producers (which should be stopped)
            std::lock_guard<std::mutex> lk(queueMtx_);
            localQueue = std::move(batchQueue_);
            batchQueue_.clear();
        }

        // Process all remaining batches in calling thread, with same helper that uses retries.
        for (auto& item : localQueue) {
            try {
                flush_front_batch(std::move(item));
            }
            catch (const std::exception& ex) {
                std::cerr << "drain_queue_on_shutdown: exception while flushing: " << ex.what() << "";
            }
        }
    }

    std::shared_ptr<StorageBackend> backend_;
    size_t queueCapacity_;
    std::atomic<bool> stopFlag_;
    size_t flusherThreads_;
    bool running_ = false;
    mutable std::mutex streamsMtx_;
    std::unordered_map<std::string, StreamHolder> streams_; // per-stream buffers and options
    std::mutex queueMtx_;
    std::deque<BatchItem> batchQueue_; // batches awaiting flush
    std::vector<std::thread> flushers_; // background flusher threads
    std::thread timerThread_; // optional timer to trigger small flushes
    std::mutex lifecycleMtx_;
    std::condition_variable cv_;

    friend class ProducerToken;
};