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

// Byte-oriented StreamBuffer: fixed-size record layout stored in a contiguous byte ring.
// - Each record is exactly recordSize bytes.
// - Capacity is expressed in number of records.
// - Append operations copy record bytes into the ring (single-record or batch).
// - take_oldest_batch pulls up to maxRecords oldest records and removes them.
// - latest(n) returns up to n newest records in newest-first order (each record appended contiguously).
// - Thread-safe via internal mutex (per-stream locking).

class StreamBuffer {
public:
    // capacityRecords: number of records buffer can hold
    // recordSize: size in bytes of each record (fixed for this buffer)
    StreamBuffer(size_t capacity_bytes, size_t recordSizeBytes)
        : recordSize_(recordSizeBytes),
        capacityBytes_((capacity_bytes / recordSizeBytes)* recordSizeBytes),
        buf_(capacityBytes_, 0),
        head_(0),
        tail_(0),
        count_(0) {
    }

    bool append(std::vector<uint8_t>&& batch) {
        if (batch.size() != recordSize_) return false;
        std::lock_guard<std::mutex> lk(mtx_);
        if (is_full_locked()) return false;
        write_at(tail_, batch.data(), recordSize_);
        tail_ = (tail_ + recordSize_) % capacityBytes_;
        ++count_;
        return true;
    }

    size_t append_batch(std::vector<uint8_t>&& batch) {
        if (batch.empty()) return 0;
        if (batch.size() % recordSize_ != 0) return 0;
        size_t recs = batch.size() / recordSize_;

        std::lock_guard<std::mutex> lk(mtx_);
        size_t freeRecs = capacity_records_locked() - count_;
        if (freeRecs == 0) return 0;

        size_t toAppendRecs = std::min(recs, freeRecs);
        size_t bytesToWrite = toAppendRecs * recordSize_;
        const uint8_t* src = batch.data();
        write_at(tail_, src, bytesToWrite);
        tail_ = (tail_ + bytesToWrite) % capacityBytes_;
        count_ += toAppendRecs;
        return toAppendRecs;
    }

    // Extract up to 'count' oldest records and return them concatenated.
    // Removes them from the buffer.
    std::vector<uint8_t> take_oldest_batch(size_t req_count) {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<uint8_t> out;
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

    // Return latest up to n records, newest-first, concatenated in returned vector.
    // This does NOT remove them.
    std::vector<uint8_t> latest(size_t n) const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<uint8_t> out;
        if (n == 0 || count_ == 0) return out;
        size_t to_take = std::min(n, count_);
        size_t bytesToRead = to_take * recordSize_;
        out.resize(bytesToRead);

        // compute start position of newest block (newest-first in output)
        // We'll fill output with newest at offset 0, next newest at offset recordSize_, ...
        size_t cur = (tail_ + capacityBytes_ - recordSize_) % capacityBytes_;
        // For performance, if the set of records we want is contiguous in the circular buffer
        // it's tricky because newest-first reverses order. We'll implement a simple loop with read_at,
        // but keep it efficient by using read_at for record-size chunks (read_at already handles wrap).
        for (size_t i = 0; i < to_take; ++i) {
            read_at(cur, out.data() + i * recordSize_, recordSize_);
            cur = (cur + capacityBytes_ - recordSize_) % capacityBytes_;
        }
        return out;
    }
    
    // Query by timestamp requires that caller knows record layout and can inspect timestamp
    // in-place. This function returns concatenated records that match a provided predicate.
    // The predicate receives a pointer to the record bytes (size recordSize_) and must return bool.
    std::vector<uint8_t> query_if(std::function<bool(const uint8_t* record)> predicate) const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<uint8_t> out;
        if (count_ == 0) return out;
        // Reserve pessimistically
        out.reserve(count_ * recordSize_);
        size_t cur = head_;
        std::vector<uint8_t> tmp(recordSize_);
        for (size_t i = 0; i < count_; ++i) {
            read_at(cur, tmp.data(), recordSize_);
            if (predicate(tmp.data())) {
                out.insert(out.end(), tmp.begin(), tmp.end());
            }
            cur = (cur + recordSize_) % capacityBytes_;
        }
        return out;
    }

    // returns records ordered oldest -> newest (easier for plotting)
    std::vector<std::pair<ts_t, std::vector<uint8_t>>> latest_parsed(size_t n) const {
        std::vector<std::pair<ts_t, std::vector<uint8_t>>> out;
        std::lock_guard<std::mutex> lk(mtx_);
        if (n == 0 || count_ == 0) return out;
        size_t take = std::min(n, count_);
        // read oldest->newest: find start index for oldest of the 'take' newest records
        size_t start_index = (count_ >= take) ? (count_ - take) : 0;
        size_t start_byte = (head_ + start_index * recordSize_) % capacityBytes_;
        // Copy concatenated bytes in two-chunk fashion
        size_t bytesToRead = take * recordSize_;
        std::vector<uint8_t> buf(bytesToRead);
        if (start_byte + bytesToRead <= capacityBytes_) {
            std::memcpy(buf.data(), &buf_[start_byte], bytesToRead);
        }
        else {
            size_t first = capacityBytes_ - start_byte;
            std::memcpy(buf.data(), &buf_[start_byte], first);
            std::memcpy(buf.data() + first, &buf_[0], bytesToRead - first);
        }
        // parse records
        out.reserve(take);
        for (size_t i = 0; i < take; ++i) {
            const uint8_t* recptr = buf.data() + i * recordSize_;
            ts_t ts;
            std::memcpy(&ts, recptr, kTimestampBytes); // host endianness
            std::vector<uint8_t> payload(recptr + kTimestampBytes, recptr + recordSize_);
            out.emplace_back(ts, std::move(payload));
        }
        return out;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mtx_);
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return count_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return count_ == 0;
    }

    bool is_full() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return is_full_locked();
    }

    // Capacity in records
    size_t capacity_records() const {
        return capacityBytes_ / recordSize_;
    }

    // Record size in bytes
    size_t record_size() const { return recordSize_; }

private:
    void write_at(size_t pos, const uint8_t* src, size_t len) {
        // len == recordSize_, and pos is aligned modulo recordSize_ if capacity is multiple.
        size_t first = std::min(len, capacityBytes_ - pos);
        std::memcpy(&buf_[pos], src, first);
        if (first < len) {
            std::memcpy(&buf_[0], src + first, len - first);
        }
    }

    void read_at(size_t pos, uint8_t* dst, size_t len) const {
        size_t first = std::min(len, capacityBytes_ - pos);
        std::memcpy(dst, &buf_[pos], first);
        if (first < len) {
            std::memcpy(dst + first, &buf_[0], len - first);
        }
    }

    bool is_full_locked() const { return count_ >= capacityBytes_ / recordSize_; }
    size_t capacity_records_locked() const { return capacityBytes_ / recordSize_; }

    mutable std::mutex mtx_; // protects all public state below
    static constexpr size_t kTimestampBytes = sizeof(ts_t);

    const size_t recordSize_;
    const size_t capacityBytes_;
    std::vector<uint8_t> buf_;
    size_t head_; // read position in bytes
    size_t tail_; // write position in bytes (next write)
    size_t count_; // number of stored records
};