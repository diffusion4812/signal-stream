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

// timestamp alias
using ts_t = std::int64_t;

class StreamBuffer {
public:
    // capacityRecords: number of records buffer can hold
    // recordSize: size in bytes of each record (fixed for this buffer)
    StreamBuffer(size_t capacity_bytes, size_t recordSizeBytes)
        : recordSize_(recordSizeBytes),
        capacityBytes_((capacity_bytes / recordSizeBytes)* recordSizeBytes),
        buf_(capacityBytes_, std::byte{ 0 }),
        head_(0),
        tail_(0),
        count_(0) {
    }

    bool append(std::vector<std::byte>&& batch) {
        if (batch.size() != recordSize_) return false;
        std::lock_guard<std::mutex> lk(mtx_);
        if (is_full_locked()) return false;
        write_at(tail_, batch.data(), recordSize_);
        tail_ = (tail_ + recordSize_) % capacityBytes_;
        ++count_;
        return true;
    }

    size_t append_batch(std::vector<std::byte>&& batch) {
        if (batch.empty()) return 0;
        if (batch.size() % recordSize_ != 0) return 0;
        size_t recs = batch.size() / recordSize_;

        std::lock_guard<std::mutex> lk(mtx_);
        size_t freeRecs = capacity_records_locked() - count_;
        if (freeRecs == 0) return 0;

        size_t toAppendRecs = std::min(recs, freeRecs);
        size_t bytesToWrite = toAppendRecs * recordSize_;
        const std::byte* src = batch.data();
        write_at(tail_, src, bytesToWrite);
        tail_ = (tail_ + bytesToWrite) % capacityBytes_;
        count_ += toAppendRecs;
        return toAppendRecs;
    }

    std::vector<std::byte> take_oldest_batch(size_t req_count) {
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
        if (count_ == 0) {
            return mpd;
        }

        ts_t rangeStart;
        ts_t rangeEnd;

        if (endTs == 0) {
            // Get latest timestamp in buffer
            size_t latestByte = (head_ + (count_ - 1) * recordSize_) % capacityBytes_;
            ts_t latestTs;
            std::memcpy(&latestTs, &buf_[latestByte], kTimestampBytes);

            ts_t prevNs = static_cast<ts_t>(startTs); // here startTs is nanoseconds in "previous seconds" mode
            rangeStart = latestTs - prevNs;
            rangeEnd = latestTs;
        }
        else {
            rangeStart = startTs;
            rangeEnd = endTs;
        }

        if (rangeStart >= rangeEnd) {
            return mpd; // invalid range
        }

        // Binary search for first index >= rangeStart
        size_t low = 0;
        size_t high = count_;
        while (low < high) {
            size_t mid = (low + high) / 2;
            size_t midByte = (head_ + mid * recordSize_) % capacityBytes_;
            ts_t ts;
            std::memcpy(&ts, &buf_[midByte], kTimestampBytes);
            if (ts < rangeStart) {
                low = mid + 1;
            }
            else {
                high = mid;
            }
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

        // Auto-switch: raw mode if fewer samples than pixels
        if (plotWidthPx == 0 || sampleCount <= plotWidthPx) {
            for (size_t i = firstIdx; i < count_; ++i) {
                size_t recByte = (head_ + i * recordSize_) % capacityBytes_;
                ts_t ts;
                std::memcpy(&ts, &buf_[recByte], kTimestampBytes);
                if (ts > rangeEnd) break;

                mpd.xs.push_back(static_cast<double>(ts) / 1e9);
                const std::byte* payload = &buf_[recByte] + kTimestampBytes;
                extractor(payload, mpd.ys);
            }
            return mpd;
        }

        // Binning mode
        ts_t binSizeNs = (rangeEnd - rangeStart) / plotWidthPx;
        ts_t alignedStart = (rangeStart / binSizeNs) * binSizeNs; // snap to pixel boundary
        ts_t binStart = alignedStart;
        ts_t binEnd = binStart + binSizeNs;

        std::vector<double> minVals(numSignals);
        std::vector<double> maxVals(numSignals);
        ts_t minTs = 0, maxTs = 0;
        bool binHasData = false;

        // Pre-allocated tempYs for one sample
        std::vector<std::vector<double>> tempYs(numSignals);
        for (auto& v : tempYs) v.reserve(1); // reserve space for one value per signal

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

            // Clear tempYs for reuse
            for (auto& v : tempYs) v.clear();

            // Extract one sample's values into tempYs
            extractor(payload, tempYs);

            // Copy values into flat vector<double>
            std::vector<double> values(numSignals);
            for (size_t s = 0; s < numSignals; ++s) {
                values[s] = tempYs[s].back();
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
                    if (minTs <= maxTs) {
                        pushPoint(minTs, minVals);
                        pushPoint(maxTs, maxVals);
                    }
                    else {
                        pushPoint(maxTs, maxVals);
                        pushPoint(minTs, minVals);
                    }
                }
                // Next bin
                binStart = binEnd;
                binEnd += binSizeNs;
                binHasData = false;
            }
        }

        // Flush last bin
        if (binHasData) {
            if (minTs <= maxTs) {
                pushPoint(minTs, minVals);
                pushPoint(maxTs, maxVals);
            }
            else {
                pushPoint(maxTs, maxVals);
                pushPoint(minTs, minVals);
            }
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
    }

    void clear() {
        std::scoped_lock<std::mutex> lk(mtx_);
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    size_t size() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return count_;
    }

    bool empty() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return count_ == 0;
    }

    bool is_full() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return is_full_locked();
    }

    size_t capacity_records() const {
        return capacityBytes_ / recordSize_;
    }

    size_t record_size() const { return recordSize_; }

private:
    void write_at(size_t pos, const std::byte* src, size_t len) {
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
    }

    bool is_full_locked() const { return count_ >= capacityBytes_ / recordSize_; }
    size_t capacity_records_locked() const { return capacityBytes_ / recordSize_; }

    mutable std::mutex mtx_;
    static constexpr size_t kTimestampBytes = sizeof(ts_t);

    const size_t recordSize_;
    const size_t capacityBytes_;
    std::vector<std::byte> buf_;
    size_t head_;
    size_t tail_;
    size_t count_;
};