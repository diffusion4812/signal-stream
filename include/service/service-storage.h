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

#include "storage-buffer.h"

#include "service-storage-backend.h"
#include "service-storage-backend-factory.h"
#include "service-source-registry.h"

namespace signal_stream {

    // timestamp alias
    using ts_t = std::int64_t;

    // Stream configuration options
    struct StreamStorageOptions {
        size_t capacity_records = 1000;
        size_t flush_batch_size = 1;
        std::chrono::milliseconds flush_interval{ 0 };
        BackendConfig backend_config = NullBackendConfig{};
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
        StreamBufferHandle(StreamBufferHandle&& other) noexcept;
        StreamBufferHandle& operator=(StreamBufferHandle&& other) noexcept;

        // Delete copy operations
        StreamBufferHandle(const StreamBufferHandle&) = delete;
        StreamBufferHandle& operator=(const StreamBufferHandle&) = delete;

        StreamBuffer* get() const noexcept { return buf; }
        explicit operator bool() const noexcept { return buf != nullptr; }
    };

    class StorageManager {
    public:
        enum StreamType {
            Archive,
            Visualization
        };

        explicit StorageManager();
        ~StorageManager();

        bool create_stream(const std::string& streamId,
            const StreamStorageOptions& opts,
            const Schema& s,
            const StreamType type);

        bool remove_stream(const std::string& streamId);

        // Acquire a token for a stream (cheap handle)
        std::optional<ProducerToken> get_producer_token(const std::string& streamId);

        SubmitResult submit_batch_for_stream(const std::string& streamId, std::vector<std::byte>&& userPayload);

        // Persistence control
        bool flush_stream(const std::string& streamId);

        size_t stream_count() const;

        std::optional<size_t> stream_size(const std::string& servicename) const;

        std::optional<StreamBufferHandle> get_buffer_handle(const std::string& streamId, const StreamType type) const;
        std::optional<float> get_arch_buffer_health(const std::string& servicename) const;

    private:
        struct ArchStreamHolder {
            std::string stream_name;
            std::unique_ptr<StreamBuffer> buffer;
            StreamStorageOptions opts;
            std::jthread flusher_thread;
            std::atomic<bool> stop_flusher{ false };
            std::mutex flusher_mtx;
            std::unique_ptr<IStorageBackend> backend; // owned backend instance
            std::condition_variable flusher_cv;
            size_t flush_batch_size;

            ArchStreamHolder(
                std::unique_ptr<StreamBuffer> buf,
                StreamStorageOptions o,
                std::string name,
                std::unique_ptr<IStorageBackend> backend
            );
            ~ArchStreamHolder();

            ArchStreamHolder(const ArchStreamHolder&) = delete;
            ArchStreamHolder& operator=(const ArchStreamHolder&) = delete;
            ArchStreamHolder(ArchStreamHolder&&) = delete;
            ArchStreamHolder& operator=(ArchStreamHolder&&) = delete;
        };

        struct VisuStreamHolder {
            std::string stream_name;
            std::unique_ptr<StreamBuffer> buffer;
            StreamStorageOptions opts;

            VisuStreamHolder(
                std::unique_ptr<StreamBuffer> buf,
                StreamStorageOptions o,
                std::string name
            );

            ~VisuStreamHolder() = default;

            VisuStreamHolder(const VisuStreamHolder&) = delete;
            VisuStreamHolder& operator=(const VisuStreamHolder&) = delete;
            VisuStreamHolder(VisuStreamHolder&&) = delete;
            VisuStreamHolder& operator=(VisuStreamHolder&&) = delete;
        };

        ArchStreamHolder* get_arch_holder(const std::string& streamId) const;
        VisuStreamHolder* get_visu_holder(const std::string& streamId) const;
        void flusher_thread_func(const std::string& streamId, ArchStreamHolder* holder, std::chrono::milliseconds interval);

        std::atomic<bool> stopFlag_;
        bool running_;

        mutable std::mutex streamsMtx_;
        std::unordered_map<std::string, std::unique_ptr<ArchStreamHolder>> arch_streams_;
        std::unordered_map<std::string, std::unique_ptr<VisuStreamHolder>> visu_streams_;

        std::mutex lifecycleMtx_;
        std::condition_variable cv_;

        friend class ProducerToken;
    };

}