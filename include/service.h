#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <cinttypes>
#include <array>

#include "project.h"

// Simple status enum for services.
enum class ServiceStatus {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error
};

struct ServiceEvent {
    std::string eventType;
    std::string message;
    // optional typed payload (JSON string or other serialized form)
    std::optional<std::string> payload;
};


// Basic sample format descriptors
enum class ElementType { UINT8, INT16, INT32, FLOAT32, FLOAT64 };
enum class Endianness { Little, Big, Native };

struct SampleFormat {
    ElementType elementType;
    uint16_t channels;
    uint32_t samplesPerChannel;
    uint32_t strideBytes; // 0 if packed interleaved
    Endianness endianness;
};

// Metadata for a single sample buffer
struct SampleMetadata {
    uint64_t timestampNs;
    uint64_t seq;
    uint32_t flags;
    SampleFormat format;
};

// Opaque handle type for zero-copy buffers
using SampleHandle = std::uintptr_t;

// Per-buffer metadata (optional wrapper)
struct SampleBufferInfo {
    const uint8_t* data;
    size_t size;
    SampleMetadata meta;
};

// Abstract base class (unchanged concept)
class IService {
public:
    virtual ~IService() = default;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual ServiceStatus Status() const = 0;
    virtual const SourceData& Source() const = 0;

    virtual bool Configure(const std::string& key, const std::string& value) { (void)key; (void)value; return true; }

    // Callback plumbing
    using ServiceCallback = std::function<void(const struct ServiceEvent&)>;
    virtual std::size_t RegisterCallback(ServiceCallback cb) = 0;
    virtual void UnregisterCallback(std::size_t handle) = 0;

    // New: buffer API for raw signal payloads
    virtual bool TryAcquireSample(SampleHandle& outHandle, const uint8_t*& outData, size_t& outSize, SampleMetadata& outMeta) = 0;
    virtual bool AcquireSample(std::chrono::milliseconds timeout, SampleHandle& outHandle, const uint8_t*& outData, size_t& outSize, SampleMetadata& outMeta) = 0;
    virtual void ReleaseSample(SampleHandle handle) = 0;
    virtual SampleFormat GetSampleFormat() const = 0;

    // Convenience: copy into vector
    virtual std::vector<uint8_t> FetchSample(SampleMetadata& outMeta) {
        SampleHandle h; const uint8_t* d; size_t s;
        if (!AcquireSample(std::chrono::milliseconds(100), h, d, s, outMeta)) return {};
        std::vector<uint8_t> v(d, d + s);
        ReleaseSample(h);
        return v;
    }
};

// Minimal base class with ring-buffer scaffold
class ServiceBase : public IService {
public:
    explicit ServiceBase(SourceData desc)
        : source_(std::move(desc)),
        status_(ServiceStatus::Stopped),
        nextCallbackHandle_(1) {
    }

    virtual ~ServiceBase() { Stop(); }

    // Lifecycle
    void Start() override {
        ServiceStatus expected = ServiceStatus::Stopped;
        if (!status_.compare_exchange_strong(expected, ServiceStatus::Starting)) return;
        if (!OnStart()) { status_.store(ServiceStatus::Error); return; }
        running_.store(true);
        worker_ = std::jthread([this] { RunLoop(); });
        status_.store(ServiceStatus::Running);
    }

    void Stop() override {
        if (status_.load() == ServiceStatus::Stopped || status_.load() == ServiceStatus::Stopping) return;
        status_.store(ServiceStatus::Stopping);
        running_.store(false);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            cv_.notify_all();
        }
        if (worker_.joinable()) worker_.join();
        OnStop();
        status_.store(ServiceStatus::Stopped);
    }

    ServiceStatus Status() const override { return status_.load(); }
    const SourceData& Source() const override { return source_; }

    // Callbacks
    std::size_t RegisterCallback(ServiceCallback cb) override {
        std::lock_guard<std::mutex> lk(cbMtx_);
        std::size_t h = nextCallbackHandle_++;
        callbacks_.emplace_back(h, std::move(cb));
        return h;
    }

    void UnregisterCallback(std::size_t handle) override {
        std::lock_guard<std::mutex> lk(cbMtx_);
        callbacks_.erase(
            std::remove_if(callbacks_.begin(), callbacks_.end(),
                [handle](auto& p) { return p.first == handle; }),
            callbacks_.end());
    }

    // Buffer API (to be implemented by concrete class)
    virtual bool TryAcquireSample(SampleHandle&, const uint8_t*&, size_t&, SampleMetadata&) override = 0;
    virtual bool AcquireSample(std::chrono::milliseconds, SampleHandle&, const uint8_t*&, size_t&, SampleMetadata&) override = 0;
    virtual void ReleaseSample(SampleHandle) override = 0;
    virtual SampleFormat GetSampleFormat() const override = 0;

protected:
    virtual bool OnStart() { return true; }
    virtual void OnStop() {}

    void PublishEvent(const struct ServiceEvent& ev) {
        std::lock_guard<std::mutex> lk(cbMtx_);
        for (auto const& p : callbacks_) {
            try { p.second(ev); }
            catch (...) {}
        }
    }

    void RunLoop() {
        while (running_.load()) {
            RunOnce();
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(200), [this] { return !running_.load(); });
        }
    }

    // Default no-op; concrete classes override
    virtual void RunOnce() {}
    SourceData source_;

private:
    mutable std::mutex cbMtx_;
    std::vector<std::pair<std::size_t, ServiceCallback>> callbacks_;
    std::size_t nextCallbackHandle_;

    std::atomic<bool> running_{ false };
    std::jthread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<ServiceStatus> status_;
};