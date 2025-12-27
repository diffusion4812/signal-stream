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

#include "schema.h"

// timestamp alias
using ts_t = std::int64_t;

class ISignalBuffer {
public:
    struct ContiguousChunk {
        const std::byte* data;
        size_t offset;
        size_t count;       // Number of elements
        Kind kind;
        bool is_valid = false;

        // Convenience helper
        size_t size_bytes() const {
            return count * element_size_from_kind(kind);
        }

    private:
        static constexpr size_t element_size_from_kind(Kind k) {
            switch (k) {
            case Kind::Int32:  return sizeof(int32_t);
            case Kind::Int64:  return sizeof(int64_t);
            case Kind::Float:  return sizeof(float);
            case Kind::Double: return sizeof(double);
            default: return 0;
            }
        }
    };

    virtual ~ISignalBuffer() = default;

    virtual size_t size() const noexcept = 0;
    virtual size_t capacity() const noexcept = 0;
    virtual bool full() const noexcept = 0;
    virtual bool empty() const noexcept = 0;

    // Returns up to 2 contiguous chunks
    virtual std::pair<ContiguousChunk, ContiguousChunk>
        get_oldest_chunks(size_t requested_count) const = 0;

    virtual void consume(size_t count) = 0;

    // NEW: Append strided data (for row-major to column-major conversion)
    virtual size_t append_strided(const std::byte* data, size_t count,
        size_t stride) = 0;

    virtual void clear() = 0;

    virtual Kind get_kind() const noexcept = 0;
};

template <typename T>
class SignalBuffer : public ISignalBuffer {
public:
    explicit SignalBuffer(size_t capacity)
        : capacity_(capacity)
        , head_(0)
        , tail_(0)
        , size_(0)
    {
        buffer_.resize(capacity_); // Pre-allocate to avoid reallocation
    }

    // Append with automatic overflow (Overwrite policy)
    size_t append_strided(const std::byte* data, size_t count,
        size_t stride) override {
        return append_strided_internal(data, count, stride, true);
    }

    // Append without overflow (Reject policy)
    size_t append_strided_no_overflow(const std::byte* data, size_t count,
        size_t stride) {
        return append_strided_internal(data, count, stride, false);
    }

    std::pair<ContiguousChunk, ContiguousChunk>
        get_oldest_chunks(size_t requested_count) const override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (requested_count > size_) {
            requested_count = size_;
        }

        ContiguousChunk first{ nullptr, 0, 0, type_to_kind<T>(), false };
        ContiguousChunk second{ nullptr, 0, 0, type_to_kind<T>(), false };

        if (requested_count == 0) {
            return { first, second };
        }

        size_t available_to_end = capacity_ - head_;

        if (requested_count <= available_to_end) {
            first = ContiguousChunk{
                reinterpret_cast<const std::byte*>(&buffer_[head_]),
                head_,
                requested_count,
                type_to_kind<T>(),
                true
            };
        }
        else {
            first = ContiguousChunk{
                reinterpret_cast<const std::byte*>(&buffer_[head_]),
                head_,
                available_to_end,
                type_to_kind<T>(),
                true
            };

            size_t wrapped_count = requested_count - available_to_end;
            second = ContiguousChunk{
                reinterpret_cast<const std::byte*>(&buffer_[0]),
                0,
                wrapped_count,
                type_to_kind<T>(),
                true
            };
        }

        return { first, second };
    }

    Kind get_kind() const noexcept override {
        return type_to_kind<T>();
    }

    // Advance head pointer after successful write
    void consume(size_t count) {
        if (count > size_) {
            throw std::runtime_error(
                "Cannot consume " + std::to_string(count) +
                " records: only " + std::to_string(size_) + " available"
            );
        }
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = (head_ + count) % capacity_;
        size_ -= count;
    }

    size_t size() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    size_t capacity() const noexcept override { return capacity_; }

    bool full() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == capacity_;
    }

    bool empty() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

    // Get element at logical index (0 = oldest)
    std::optional<T> at(size_t index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= size_) return std::nullopt;

        size_t physical_index = (head_ + index) % capacity_;
        return buffer_[physical_index];
    }

    // Clear buffer
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    // Get head/tail positions (for debugging)
    size_t head() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return head_;
    }

    size_t tail() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tail_;
    }

private:
    size_t append_strided_internal(const std::byte* data, size_t count,
        size_t stride, bool allow_overwrite) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (count == 0) return 0;

        // For Reject policy: limit count to available space
        if (!allow_overwrite) {
            const size_t available = capacity_ - size_;
            count = std::min(count, available);
            if (count == 0) return 0;
        }

        const T* typed_data = reinterpret_cast<const T*>(data);
        const size_t stride_elements = stride / sizeof(T);

        for (size_t i = 0; i < count; ++i) {
            buffer_[tail_] = typed_data[i * stride_elements];
            tail_ = (tail_ + 1) % capacity_;

            if (size_ < capacity_) {
                ++size_;
            }
            else if (allow_overwrite) {
                // Circular: advance head
                head_ = (head_ + 1) % capacity_;
            }
        }

        return count;
    }

    size_t capacity_;
    std::vector<T> buffer_;
    size_t head_;   // Index of oldest element
    size_t tail_;   // Index where next element will be written
    size_t size_;   // Current number of elements
    mutable std::mutex mutex_;

    // Helper function
    template<typename U>
    static constexpr Kind type_to_kind() {
        if constexpr (std::is_same_v<U, int32_t>) return Kind::Int32;
        if constexpr (std::is_same_v<U, int64_t>) return Kind::Int64;
        if constexpr (std::is_same_v<U, float>) return Kind::Float;
        if constexpr (std::is_same_v<U, double>) return Kind::Double;
		throw std::runtime_error("Unsupported type for SignalBuffer");
    }
};

class StreamBuffer {
public:
    enum class OverflowPolicy {
        Overwrite,  // Circular buffer: overwrite oldest data (default)
        Reject,     // Reject new data when full, keep oldest data
        Throw       // Throw exception on overflow attempt
    };

    struct BatchChunks {
        size_t total_count;
        // First contiguous region (always present if total_count > 0)
        std::unordered_map<size_t, ISignalBuffer::ContiguousChunk> first_chunk;
        // Second region (only if ring wraps)
        std::unordered_map<size_t, ISignalBuffer::ContiguousChunk> second_chunk;
        bool has_wrap() const { return !second_chunk.empty(); }
    };

    // capacityRecords: number of records buffer can hold
    StreamBuffer(const Schema& s, size_t c, OverflowPolicy policy = OverflowPolicy::Reject)
        : schema_(s)
        , capacity_records_(c)
        , overflow_policy_(policy)
    {
        // Initialize signal buffers for each schema field
        for (const auto& field : schema_.fields()) {
            std::unique_ptr<ISignalBuffer> bufPtr;

            switch (field.kind) {
            case Kind::Int32:
                bufPtr = std::make_unique<SignalBuffer<int32_t>>(capacity_records_);
                break;

            case Kind::Int64:
                bufPtr = std::make_unique<SignalBuffer<int64_t>>(capacity_records_);
                break;

            case Kind::Float:
                bufPtr = std::make_unique<SignalBuffer<float>>(capacity_records_);
                break;

            case Kind::Double:
                bufPtr = std::make_unique<SignalBuffer<double>>(capacity_records_);
                break;

            default:
                throw std::runtime_error("Unsupported field kind in StreamBuffer ctor");
            }

            // Insert into map: key = field index, value = the new buffer
            buffers_.emplace(field.idx, std::move(bufPtr));
        }
    }

    struct AppendResult {
        size_t appended;    // Records successfully written to buffer
        size_t overwritten; // Old records overwritten (Overwrite policy)
        size_t rejected;    // New records rejected (Reject policy)

        bool had_overflow() const { return overwritten > 0; }
        bool had_rejection() const { return rejected > 0; }
        bool full_success() const { return overwritten == 0 && rejected == 0; }

        explicit operator bool() const { return appended > 0; }
    };

    AppendResult append(std::vector<std::byte>&& batch) {
		if (batch.empty()) throw std::runtime_error("Cannot append empty batch to StreamBuffer");

        const size_t instanceSize = schema_.instance_size();
        if (batch.size() % instanceSize != 0) throw std::runtime_error("Append batch size " + std::to_string(batch.size()) + " is not a multiple of instance size " + std::to_string(instanceSize));

        const size_t recs = batch.size() / instanceSize;

        std::lock_guard<std::mutex> lk(mtx_);

        // Check current buffer state
        const size_t current_size = buffers_.at(0)->size();
        const size_t available_space = capacity_records_ - current_size;

        // Determine action based on overflow policy
        if (recs > available_space) {
            switch (overflow_policy_) {
            case OverflowPolicy::Throw: {
                throw std::runtime_error(
                    "Buffer overflow: attempted to append " + std::to_string(recs) +
                    " records but only " + std::to_string(available_space) + " available"
                );
            }

            case OverflowPolicy::Reject: {
                // Only append what fits, reject the rest
                const size_t to_append = available_space;
                const size_t rejected = recs - available_space;

                if (to_append == 0) {
                    return { 0, 0, rejected };
                }

                // Append partial batch
                const std::byte* batchPtr = batch.data();
                for (const auto& field : schema_.fields()) {
                    auto* bufIface = buffers_.at(field.idx).get();
                    const std::byte* colStart = batchPtr + field.offset;
                    bufIface->append_strided(colStart, to_append, instanceSize);
                }

                return { to_append, 0, rejected };
            }

            case OverflowPolicy::Overwrite: {
                // Circular buffer: append all, overwrite oldest
                const size_t overwritten = recs - available_space;

                const std::byte* batchPtr = batch.data();
                for (const auto& field : schema_.fields()) {
                    auto* bufIface = buffers_.at(field.idx).get();
                    const std::byte* colStart = batchPtr + field.offset;
                    bufIface->append_strided(colStart, recs, instanceSize);
                }

                return { recs, overwritten, 0 };
            }
            }
        }

        // No overflow: append normally
        const std::byte* batchPtr = batch.data();
        for (const auto& field : schema_.fields()) {
            auto* bufIface = buffers_.at(field.idx).get();
            const std::byte* colStart = batchPtr + field.offset;
            bufIface->append_strided(colStart, recs, instanceSize);
        }

        return { recs, 0, 0 };
    }

    BatchChunks get_batch_chunks(size_t requested_count) const {
        BatchChunks result;
        result.total_count = 0;

        if (buffers_.empty()) return result;

        // Limit returned records to available records
        size_t actual_count = std::min(requested_count, buffers_.at(0)->size());

        if (actual_count == 0) return result;

        result.total_count = actual_count;

        // Get chunks for all signals
        for (const auto& [idx, buffer] : buffers_) {
            auto [chunk1, chunk2] = buffer->get_oldest_chunks(actual_count);

            if (chunk1.is_valid) {
                result.first_chunk[idx] = ISignalBuffer::ContiguousChunk{
                    chunk1.data,
                    0,
                    chunk1.count,
					schema_.fields().at(idx).kind,
                    true
                };
            }

            if (chunk2.is_valid) {
                result.second_chunk[idx] = ISignalBuffer::ContiguousChunk{
                    chunk2.data,
                    0,
                    chunk2.count,
                    schema_.fields().at(idx).kind,
                    true
                };
            }
        }

        return result;
    }

    void consume_batch(size_t count) {
        // Pre-validation: check if operation is valid
        if (count == 0) {
            return; // No-op, avoid unnecessary iteration
        }

        size_t min_available = std::numeric_limits<size_t>::max();
        for (const auto& [idx, buffer] : buffers_) {
            min_available = std::min(min_available, buffer->size());
        }

		size_t to_consume = std::min(count, min_available);
        for (auto& [idx, buffer] : buffers_) {
            buffer->consume(to_consume);
        }
    }

    void clear() {
        std::scoped_lock<std::mutex> lk(mtx_);
		buffers_.clear();
    }

    size_t size() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return buffers_.at(0)->size();
    }

    bool empty() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return buffers_.at(0)->size() == 0;
    }

    bool is_full() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return buffers_.at(0)->size() >= capacity_records_;
    }

    size_t record_size() const { return schema_.instance_size(); }
    size_t capacity_records() const { return capacity_records_; }
    const Schema& get_schema() const { return schema_; }

private:
    mutable std::mutex mtx_;

    const Schema& schema_;
    size_t capacity_records_;
    OverflowPolicy overflow_policy_;
    std::unordered_map<size_t, std::unique_ptr<ISignalBuffer>> buffers_;
};