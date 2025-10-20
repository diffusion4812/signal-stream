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

    // Append one record (data must point to recordSize_ bytes). Returns false if full.
    bool append(const uint8_t* data, size_t len) {
        if (len != recordSize_) return false;
        if (is_full()) return false;
        write_at(tail_, data, recordSize_);
        tail_ = (tail_ + recordSize_) % capacityBytes_;
        ++count_;
        return true;
    }

    // Append contiguous batch of records (len must be multiple of recordSize_). Returns number of records appended.
    size_t append_batch(const uint8_t* data, size_t len) {
        if (len == 0) return 0;
        if (len % recordSize_ != 0) return 0;
        size_t recs = len / recordSize_;
        size_t appended = 0;
        for (size_t i = 0; i < recs; ++i) {
            if (!append(data + i * recordSize_, recordSize_)) break;
            ++appended;
        }
        return appended;
    }

    // Extract up to 'count' oldest records and return them concatenated.
    // Removes them from the buffer.
    std::vector<uint8_t> take_oldest_batch(size_t count) {
        std::vector<uint8_t> out;
        if (count == 0 || empty()) return out;
        size_t to_take = std::min(count, count_);
        out.resize(to_take * recordSize_);
        for (size_t i = 0; i < to_take; ++i) {
            read_at(head_, out.data() + i * recordSize_, recordSize_);
            head_ = (head_ + recordSize_) % capacityBytes_;
        }
        count_ -= to_take;
        // If buffer was emptied, reset pointers to 0 for simpler reuse (optional)
        if (count_ == 0) { head_ = tail_ = 0; }
        return out;
    }

    // Return latest up to n records, newest-first, concatenated in returned vector.
    // This does NOT remove them.
    std::vector<uint8_t> latest(size_t n) const {
        std::vector<uint8_t> out;
        if (n == 0 || empty()) return out;
        size_t to_take = std::min(n, count_);
        out.resize(to_take * recordSize_);
        // walk from newest backwards
        size_t cur = (tail_ + capacityBytes_ - recordSize_) % capacityBytes_;
        for (size_t i = 0; i < to_take; ++i) {
            // place newest-first: output [0..] = newest, next newest, ...
            read_at(cur, out.data() + i * recordSize_, recordSize_);
            cur = (cur + capacityBytes_ - recordSize_) % capacityBytes_;
        }
        return out;
    }

    // Number of stored records
    size_t size() const {
        return count_;
    }

    // Capacity in records
    size_t capacity_records() const {
        return capacityBytes_ / recordSize_;
    }
    
    // Query by timestamp requires that caller knows record layout and can inspect timestamp
    // in-place. This function returns concatenated records that match a provided predicate.
    // The predicate receives a pointer to the record bytes (size recordSize_) and must return bool.
    std::vector<uint8_t> query_if(std::function<bool(const uint8_t* record)> predicate) const {
        std::vector<uint8_t> out;
        if (empty()) return out;
        // We'll collect matches into a temp vector
        std::vector<uint8_t> tmp;
        tmp.reserve(count_ * recordSize_);
        size_t cur = head_;
        for (size_t i = 0; i < count_; ++i) {
            std::vector<uint8_t> rec(recordSize_);
            read_at(cur, rec.data(), recordSize_);
            if (predicate(rec.data())) {
                tmp.insert(tmp.end(), rec.begin(), rec.end());
            }
            cur = (cur + recordSize_) % capacityBytes_;
        }
        return tmp;
    }

    bool empty() const { return count_ == 0; }
    bool is_full() const { return count_ >= capacity_records(); }

    // Clear buffer (drops all records)
    void clear() {
        head_ = 0;
        count_ = 0;
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

    const size_t recordSize_;
    const size_t capacityBytes_;
    std::vector<uint8_t> buf_;
    size_t head_; // read position in bytes
    size_t tail_; // write position in bytes (next write)
    size_t count_; // number of stored records
};