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
    virtual ~ISignalBuffer() = default;

    virtual size_t size() const noexcept = 0;
    virtual size_t capacity() const noexcept = 0;
    virtual bool full() const noexcept = 0;
    virtual bool empty() const noexcept = 0;

    // NEW: Append strided data (for row-major to column-major conversion)
    virtual size_t append_strided(const std::byte* data, size_t count,
        size_t stride) = 0;

    virtual void clear() = 0;
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
};

class StreamBuffer {
public:
    enum class OverflowPolicy {
        Overwrite,  // Circular buffer: overwrite oldest data (default)
        Reject,     // Reject new data when full, keep oldest data
        Throw       // Throw exception on overflow attempt
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
        if (batch.empty()) return { 0, 0, 0 };

        const size_t instanceSize = schema_.instance_size();
        if (batch.size() % instanceSize != 0) return { 0, 0, 0 };

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

    /*std::vector<std::byte> take_oldest_batch(size_t req_count) {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::byte> out;
        if (req_count == 0 || count_ == 0) return out;
        size_t to_take = std::min(req_count, count_);
        size_t bytesToRead = to_take * recordSize_;
        out.resize(bytesToRead);

        if (head_ + bytesToRead <= capacityBytes_) {
            std::memcpy(out.data(), &buf_[head_], bytesToRead);
            head_ = (head_ + bytesToRead) % capacityBytes_;
        }
        else {
            size_t first = capacityBytes_ - head_;
            std::memcpy(out.data(), &buf_[head_], first);
            std::memcpy(out.data() + first, &buf_[0], bytesToRead - first);
            head_ = (head_ + bytesToRead) % capacityBytes_;
        }

        count_ -= to_take;
        if (count_ == 0) { head_ = tail_ = 0; }
        return out;
    }

    std::vector<std::byte> latest(size_t n) const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::byte> out;
        if (n == 0 || count_ == 0) {
            out.resize(recordSize_);
            return out; // Return one empty record
        }
        size_t to_take = std::min(n, count_);
        size_t bytesToRead = to_take * recordSize_;
        out.resize(bytesToRead);

        size_t cur = (tail_ + capacityBytes_ - recordSize_) % capacityBytes_;
        for (size_t i = 0; i < to_take; ++i) {
            read_at(cur, out.data() + i * recordSize_, recordSize_);
            cur = (cur + capacityBytes_ - recordSize_) % capacityBytes_;
        }
        return out;
    }

    std::vector<std::byte> query_if(std::function<bool(const std::byte* record)> predicate) const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::byte> out;
        if (count_ == 0) return out;
        out.reserve(count_ * recordSize_);
        size_t cur = head_;
        std::vector<std::byte> tmp(recordSize_);
        for (size_t i = 0; i < count_; ++i) {
            read_at(cur, tmp.data(), recordSize_);
            if (predicate(tmp.data())) {
                out.insert(out.end(), tmp.begin(), tmp.end());
            }
            cur = (cur + recordSize_) % capacityBytes_;
        }
        return out;
    }

    std::vector<std::pair<ts_t, std::vector<std::byte>>> latest_parsed(size_t n) const {
        std::vector<std::pair<ts_t, std::vector<std::byte>>> out;
        std::lock_guard<std::mutex> lk(mtx_);
        if (n == 0 || count_ == 0) {
            out.resize(1);
            out[0].first = 0;
            out[0].second.resize(recordSize_);
            return out; // Return one empty record
        }
        size_t take = std::min(n, count_);
        size_t start_index = (count_ >= take) ? (count_ - take) : 0;
        size_t start_byte = (head_ + start_index * recordSize_) % capacityBytes_;
        size_t bytesToRead = take * recordSize_;
        std::vector<std::byte> buf(bytesToRead);

        if (start_byte + bytesToRead <= capacityBytes_) {
            std::memcpy(buf.data(), &buf_[start_byte], bytesToRead);
        }
        else {
            size_t first = capacityBytes_ - start_byte;
            std::memcpy(buf.data(), &buf_[start_byte], first);
            std::memcpy(buf.data() + first, &buf_[0], bytesToRead - first);
        }

        out.reserve(take);
        for (size_t i = 0; i < take; ++i) {
            const std::byte* recptr = buf.data() + i * recordSize_;
            ts_t ts;
            std::memcpy(&ts, recptr, kTimestampBytes);
            std::vector<std::byte> payload(recptr + kTimestampBytes, recptr + recordSize_);
            out.emplace_back(ts, std::move(payload));
        }
        return out;
    }

    // NEW: Multi-signal export for ImPlot
    struct MultiPlotData {
        std::vector<double> xs;                   // shared timestamps
        std::vector<std::vector<double>> ys;      // one vector per signal
    };

    template<typename ExtractorFn>
    MultiPlotData range_multi_plot_data(ts_t startTs, ts_t endTs, size_t numSignals, ExtractorFn extractor, size_t plotWidthPx = 0, size_t maxPoints = 0) const {
        MultiPlotData mpd;
        mpd.ys.resize(numSignals);

        std::lock_guard<std::mutex> lk(mtx_);
        if (count_ == 0) return mpd;

        ts_t rangeStart, rangeEnd;
        if (endTs == 0) {
            size_t latestByte = (head_ + (count_ - 1) * recordSize_) % capacityBytes_;
            ts_t latestTs;
            std::memcpy(&latestTs, &buf_[latestByte], kTimestampBytes);
            ts_t prevNs = static_cast<ts_t>(startTs); // nanoseconds in "previous seconds" mode
            rangeStart = latestTs - prevNs;
            rangeEnd = latestTs;
        }
        else {
            rangeStart = startTs;
            rangeEnd = endTs;
        }
        if (rangeStart >= rangeEnd) return mpd;

        // Binary search for first index >= rangeStart
        size_t low = 0, high = count_;
        while (low < high) {
            size_t mid = (low + high) / 2;
            size_t midByte = (head_ + mid * recordSize_) % capacityBytes_;
            ts_t ts;
            std::memcpy(&ts, &buf_[midByte], kTimestampBytes);
            if (ts < rangeStart) low = mid + 1;
            else high = mid;
        }
        size_t firstIdx = low;

        // Count samples in range
        size_t sampleCount = 0;
        for (size_t i = firstIdx; i < count_; ++i) {
            size_t recByte = (head_ + i * recordSize_) % capacityBytes_;
            ts_t ts;
            std::memcpy(&ts, &buf_[recByte], kTimestampBytes);
            if (ts > rangeEnd) break;
            sampleCount++;
        }

        // Safety cap
        if (maxPoints > 0 && sampleCount > maxPoints) {
            plotWidthPx = maxPoints / 2; // each bin produces up to 2 points
        }

        // RAW MODE: fewer samples than pixels
        if (plotWidthPx == 0 || sampleCount <= plotWidthPx) {
            for (auto& sigVec : mpd.ys) sigVec.reserve(sampleCount);
            for (size_t i = firstIdx; i < count_; ++i) {
                size_t recByte = (head_ + i * recordSize_) % capacityBytes_;
                ts_t ts;
                std::memcpy(&ts, &buf_[recByte], kTimestampBytes);
                if (ts > rangeEnd) break;
                mpd.xs.push_back(static_cast<double>(ts) / 1e9);
                const std::byte* payload = &buf_[recByte] + kTimestampBytes;
                extractor(payload, mpd.ys); // push_back mode
            }
            return mpd;
        }

        // BINNING MODE
        ts_t binSizeNs = (rangeEnd - rangeStart) / plotWidthPx;
        ts_t alignedStart = (rangeStart / binSizeNs) * binSizeNs; // snap to pixel boundary
        ts_t binStart = alignedStart;
        ts_t binEnd = binStart + binSizeNs;

        std::vector<double> minVals(numSignals), maxVals(numSignals);
        std::vector<double> values(numSignals); // preallocated, reused each iteration
        ts_t minTs = 0, maxTs = 0;
        bool binHasData = false;

        // Preallocated tempYs with size 1 per signal (overwrite mode)
        std::vector<std::vector<double>> tempYs(numSignals, std::vector<double>(1));

        auto pushPoint = [&](ts_t pointTs, const std::vector<double>& vals) {
            mpd.xs.push_back(static_cast<double>(pointTs) / 1e9);
            for (size_t s = 0; s < numSignals; ++s) {
                mpd.ys[s].push_back(vals[s]);
            }
            };

        for (size_t i = firstIdx; i < count_; ++i) {
            size_t recByte = (head_ + i * recordSize_) % capacityBytes_;
            ts_t ts;
            std::memcpy(&ts, &buf_[recByte], kTimestampBytes);
            if (ts > rangeEnd) break;

            const std::byte* payload = &buf_[recByte] + kTimestampBytes;

            // Overwrite mode: extractor writes into tempYs[s][0]
            extractor(payload, tempYs);

            // Copy into preallocated values array
            for (size_t s = 0; s < numSignals; ++s) {
                values[s] = tempYs[s][0];
            }

            if (!binHasData) {
                minVals = values;
                maxVals = values;
                minTs = ts;
                maxTs = ts;
                binHasData = true;
            }
            else {
                for (size_t s = 0; s < numSignals; ++s) {
                    if (values[s] < minVals[s]) { minVals[s] = values[s]; minTs = ts; }
                    if (values[s] > maxVals[s]) { maxVals[s] = values[s]; maxTs = ts; }
                }
            }

            if (ts >= binEnd) {
                if (binHasData) {
                    if (minTs <= maxTs) { pushPoint(minTs, minVals); pushPoint(maxTs, maxVals); }
                    else { pushPoint(maxTs, maxVals); pushPoint(minTs, minVals); }
                }
                binStart = binEnd;
                binEnd += binSizeNs;
                binHasData = false;
            }
        }

        // Flush last bin
        if (binHasData) {
            if (minTs <= maxTs) { pushPoint(minTs, minVals); pushPoint(maxTs, maxVals); }
            else { pushPoint(maxTs, maxVals); pushPoint(minTs, minVals); }
        }

        return mpd;
    }

    template<typename ExtractorFn>
    MultiPlotData latest_multi_plot_data(size_t n, size_t numSignals, ExtractorFn extractor) const {
        MultiPlotData mpd;
        mpd.ys.resize(numSignals); // always have numSignals inner vectors

        std::lock_guard<std::mutex> lk(mtx_);
        if (n == 0 || count_ == 0) {
            // xs stays empty, each ys[i] stays empty
            return mpd;
        }

        size_t take = std::min(n, count_);
        mpd.xs.reserve(take);
        for (auto& y : mpd.ys) y.reserve(take);

        size_t start_index = (count_ >= take) ? (count_ - take) : 0;
        size_t start_byte = (head_ + start_index * recordSize_) % capacityBytes_;
        std::vector<std::byte> buf(take * recordSize_);

        if (start_byte + buf.size() <= capacityBytes_) {
            std::memcpy(buf.data(), &buf_[start_byte], buf.size());
        }
        else {
            size_t first = capacityBytes_ - start_byte;
            std::memcpy(buf.data(), &buf_[start_byte], first);
            std::memcpy(buf.data() + first, &buf_[0], buf.size() - first);
        }

        for (size_t i = 0; i < take; ++i) {
            const std::byte* recptr = buf.data() + i * recordSize_;
            ts_t ts;
            std::memcpy(&ts, recptr, kTimestampBytes);
            mpd.xs.push_back(static_cast<double>(ts) / 1e9);

            const std::byte* payload = recptr + kTimestampBytes;
            extractor(payload, mpd.ys); // fill all signals in one go
        }
        return mpd;
    }*/

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

private:
    /*void write_at(size_t pos, const std::byte* src, size_t len) {
        size_t first = std::min(len, capacityBytes_ - pos);
        std::memcpy(&buf_[pos], src, first);
        if (first < len) {
            std::memcpy(&buf_[0], src + first, len - first);
        }
    }

    void read_at(size_t pos, std::byte* dst, size_t len) const {
        size_t first = std::min(len, capacityBytes_ - pos);
        std::memcpy(dst, &buf_[pos], first);
        if (first < len) {
            std::memcpy(dst + first, &buf_[0], len - first);
        }
    }*/

    mutable std::mutex mtx_;

    const Schema& schema_;
    size_t capacity_records_;
    OverflowPolicy overflow_policy_;
    std::unordered_map<size_t, std::unique_ptr<ISignalBuffer>> buffers_;
};