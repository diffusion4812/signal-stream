#pragma once

#include <vector>
#include <atomic>
#include <optional>
#include <cassert>
#include <mutex>
#include <thread>
#include <iostream>
#include <sstream>
#include <memory>
#include <chrono>

template<typename T>
class SPSC_CircularBuffer {
public:
    explicit SPSC_CircularBuffer(size_t capacity)
        : buf_(capacity), capacity_(capacity), head_(0), tail_(0) {
        assert(capacity > 0);
    }

    // Non-blocking push: returns false if full
    bool push(const T& item) {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto next = increment(tail);
        if (next == head_.load(std::memory_order_acquire)) return false; // full
        buf_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool push(T&& item) {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto next = increment(tail);
        if (next == head_.load(std::memory_order_acquire)) return false; // full
        buf_[tail] = std::move(item);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Non-blocking pop: returns false if empty
    bool pop(T& out) {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false; // empty
        out = std::move(buf_[head]);
        head_.store(increment(head), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return head_.load() == tail_.load();
    }

    size_t capacity() const noexcept { return capacity_; }

    // Approximate size (may be inconsistent in concurrent use)
    size_t size() const noexcept {
        auto head = head_.load(std::memory_order_acquire);
        auto tail = tail_.load(std::memory_order_acquire);
        if (tail >= head) return tail - head;
        return capacity_ - head + tail;
    }

    void clear() {
        head_.store(0);
        tail_.store(0);
    }

    std::vector<T> buf_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;

private:
    size_t increment(size_t idx) const noexcept {
        return (idx + 1) % capacity_;
    }

    const size_t capacity_;
};

template<typename T>
class BlockingCircularBuffer {
public:
    explicit BlockingCircularBuffer(size_t capacity)
        : buf_(capacity), capacity_(capacity), head_(0), tail_(0), size_(0) {
        assert(capacity > 0);
    }

    // Blocking push with optional timeout. Returns false on timeout.
    bool push(const T& item, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (timeout == std::chrono::milliseconds::max()) {
            not_full_.wait(lk, [this] { return size_ < capacity_; });
        }
        else {
            if (!not_full_.wait_for(lk, timeout, [this] { return size_ < capacity_; })) return false;
        }
        buf_[tail_] = item;
        tail_ = (tail_ + 1) % capacity_;
        ++size_;
        lk.unlock();
        not_empty_.notify_one();
        return true;
    }

    bool push(T&& item, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (timeout == std::chrono::milliseconds::max()) {
            not_full_.wait(lk, [this] { return size_ < capacity_; });
        }
        else {
            if (!not_full_.wait_for(lk, timeout, [this] { return size_ < capacity_; })) return false;
        }
        buf_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;
        lk.unlock();
        not_empty_.notify_one();
        return true;
    }

    // Blocking pop with optional timeout. Returns false on timeout.
    bool pop(T& out, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (timeout == std::chrono::milliseconds::max()) {
            not_empty_.wait(lk, [this] { return size_ > 0; });
        }
        else {
            if (!not_empty_.wait_for(lk, timeout, [this] { return size_ > 0; })) return false;
        }
        out = std::move(buf_[head_]);
        head_ = (head_ + 1) % capacity_;
        --size_;
        lk.unlock();
        not_full_.notify_one();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return size_ == 0;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return size_;
    }

    size_t capacity() const noexcept { return capacity_; }

    void clear() {
        std::lock_guard<std::mutex> lk(mtx_);
        head_ = tail_ = size_ = 0;
        not_full_.notify_all();
    }

    std::vector<T> buf_;
    size_t head_;
    size_t tail_;

private:
    const size_t capacity_;
    size_t size_;
    mutable std::mutex mtx_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};