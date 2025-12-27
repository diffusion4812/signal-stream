#include "service-storage.h"

// ============================================================================
// ProducerToken Implementation
// ============================================================================

ProducerToken::ProducerToken(StorageManager* mgr, std::string id)
    : mgr_(mgr), streamId_(std::move(id)) {
}

SubmitResult ProducerToken::try_submit(std::vector<std::byte>&& batch) const {
    if (!mgr_) return SubmitResult::UnknownStream;
    return mgr_->submit_batch_for_stream(streamId_, std::move(batch));
}

// ============================================================================
// StreamBufferHandle Implementation
// ============================================================================

StreamBufferHandle::StreamBufferHandle(StreamBuffer* b) noexcept
    : buf(b) {
}

StreamBufferHandle::StreamBufferHandle(StreamBufferHandle&& other) noexcept
    : buf(other.buf) {
    other.buf = nullptr;
}

StreamBufferHandle& StreamBufferHandle::operator=(StreamBufferHandle&& other) noexcept {
    if (this != &other) {
        buf = other.buf;
        other.buf = nullptr;
    }
    return *this;
}

// ============================================================================
// StorageManager::StorageStreamHolder Implementation
// ============================================================================

StorageManager::StorageStreamHolder::StorageStreamHolder(
    std::unique_ptr<StreamBuffer> buf,
    StreamStorageOptions o,
    std::string stream,
    std::unique_ptr<IStorageBackend> backend)
    : buffer(std::move(buf)),
    opts(std::move(o)),
    stream_name(std::move(stream)),
    backend(std::move(backend)),
    flush_batch_size(o.flush_batch_size) {
}

StorageManager::StorageStreamHolder::~StorageStreamHolder() {
    // Step 1: Stop flusher thread
    stop_flusher.store(true, std::memory_order_release);
    flusher_cv.notify_all();

    if (flusher_thread.joinable()) {
        flusher_thread.join();
    }

    // Step 2: Flush remaining data
    if (buffer && buffer->size() > 0) {
        try {
            // Flush all remaining data
            while (buffer->size() > 0) {
                auto chunks = buffer->get_batch_chunks(flush_batch_size);
                if (chunks.total_count == 0) break;

                if (!backend->write_batch_two_pass(stream_name, chunks)) {
                    std::cerr << "Warning: Failed to flush stream '" << stream_name << "' during cleanup" << std::endl;
                    break;
                }

                buffer->consume_batch(chunks.total_count);
            }
        }
        catch (const std::exception& e) {
            //std::cerr << "Error flushing stream '" << stream_name << "' during cleanup: " << e.what() << std::endl;
        }
    }
}

// ============================================================================
// StorageManager Implementation
// ============================================================================

StorageManager::StorageManager()
    : stopFlag_(false),
    running_(false) {
}

StorageManager::~StorageManager() {
}

bool StorageManager::create_stream(const std::string& streamId,
    const StreamStorageOptions& opts,
    const Schema& s) {
    std::scoped_lock lk(streamsMtx_);
    if (streams_.contains(streamId)) {
        return true;
    }

    std::unique_ptr<StreamBuffer> buf = std::make_unique<StreamBuffer>(
        s,
        opts.capacity_records,
        StreamBuffer::OverflowPolicy::Overwrite
    );

    // Create holder with backend reference
    auto holder = std::make_unique<StorageStreamHolder>(
        std::move(buf),
        opts,
        streamId,
        std::make_unique<ParquetBackend>(std::filesystem::path("data_storage"), s)
    );

    if (opts.flush_interval.count() > 0) {
        StorageStreamHolder* holder_ptr = holder.get();
        holder->flusher_thread = std::jthread(
            &StorageManager::flusher_thread_func,
            this,
            streamId,
            holder_ptr,
            opts.flush_interval
        );
    }

    streams_[streamId] = std::move(holder);
    return true;
}

bool StorageManager::remove_stream(const std::string& streamId) {
    std::unique_ptr<StorageStreamHolder> holder_to_destroy;

    {
        std::scoped_lock<std::mutex> lk(streamsMtx_);
        auto it = streams_.find(streamId);
        if (it == streams_.end()) return true;

        holder_to_destroy = std::move(it->second);
        streams_.erase(it);
    }

    return true;
}

std::optional<ProducerToken> StorageManager::get_producer_token(const std::string& streamId) {
    std::scoped_lock<std::mutex> lk(streamsMtx_);
    if (!streams_.count(streamId)) return std::nullopt;
    return ProducerToken(this, streamId);
}

SubmitResult StorageManager::submit_batch_for_stream(
    const std::string& streamId,
    std::vector<std::byte>&& userPayload) {
    StorageStreamHolder* holder = get_holder(streamId);
    if (!holder) return SubmitResult::UnknownStream;

    auto res = holder->buffer->append(std::move(userPayload));
    if (!res.full_success()) {
        return SubmitResult::BackPressure;
    }

    return SubmitResult::Accepted;
}

bool StorageManager::flush_stream(const std::string& streamId) {
    StorageStreamHolder* holder = get_holder(streamId);
    if (!holder) return false;

    while (true) {
        // Get zero-copy view into ring buffer
        StreamBuffer::BatchChunks chunks = holder->buffer->get_batch_chunks(holder->opts.flush_batch_size);

        if (chunks.total_count == 0) break;

        // Write immediately while pointers are valid
        if (!holder->backend->write_batch_two_pass(streamId, chunks)) {
            return false;
        }

        // Consume only after successful write
        holder->buffer->consume_batch(chunks.total_count);
    }

    return true;
}

void StorageManager::flusher_thread_func(const std::string& streamId, StorageStreamHolder* holder, std::chrono::milliseconds interval) {
    while (!holder->stop_flusher.load(std::memory_order_acquire) &&
        !stopFlag_.load(std::memory_order_acquire)) {

        // Wait on condition variable with timeout
            {
                std::unique_lock<std::mutex> lock(holder->flusher_mtx);
                holder->flusher_cv.wait_for(lock, interval, [holder, this]() {
                    return holder->stop_flusher.load(std::memory_order_acquire) ||
                        stopFlag_.load(std::memory_order_acquire);
                    });
            }

            // Check stop condition again
            if (holder->stop_flusher.load(std::memory_order_acquire) ||
                stopFlag_.load(std::memory_order_acquire)) {
                break;
            }

            // Do work
            if (holder->buffer->size() > 0) {
                try {
                    flush_stream(streamId);
                }
                catch (const std::exception& e) {
                    std::cerr << "Flusher error: " << e.what() << std::endl;
                }
            }
    }
    // Thread exits, join in destructor will complete
}

size_t StorageManager::stream_count() const {
    std::scoped_lock lk(streamsMtx_);
    return streams_.size();
}

std::optional<size_t> StorageManager::stream_size(const std::string& servicename) const {
    StorageStreamHolder* holder = get_holder(servicename);
    if (!holder) return std::nullopt;
    return holder->buffer->size();
}

std::optional<StreamBufferHandle> StorageManager::GetBufferHandle(const std::string& streamId) {
    std::unique_lock lk(streamsMtx_);
    auto it = streams_.find(streamId);
    if (it == streams_.end()) {
        return std::nullopt;
    }
    // Lock is released here � caller must lock if needed
    return std::make_optional<StreamBufferHandle>(it->second->buffer.get());
}

std::optional<float> StorageManager::GetBufferHealth(const std::string& servicename) const {
    StorageStreamHolder* holder = get_holder(servicename);
    if (!holder) return std::nullopt;
    return static_cast<float>(holder->buffer->size()) / holder->buffer->capacity_records();
}

StorageManager::StorageStreamHolder* StorageManager::get_holder(const std::string& streamId) const {
    std::scoped_lock<std::mutex> lk(streamsMtx_);
    auto it = streams_.find(streamId);
	return (it == streams_.end()) ? nullptr : it->second.get();
}