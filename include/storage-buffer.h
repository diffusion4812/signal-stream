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