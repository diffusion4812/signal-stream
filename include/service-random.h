#pragma once

#include <vector>
#include <queue>
#include <random>
#include <atomic>
#include <thread>
#include <mutex>

#include "service.h"

class RandomDataService : public ServiceBase {
public:
    // Create with a descriptor and a buffer capacity
    static std::shared_ptr<RandomDataService> Create(const Schema& schema) {
        return std::make_shared<RandomDataService>(schema);
    }

    RandomDataService(const Schema& schema)
        : ServiceBase(schema),
        gen_(std::random_device{}()),
        dist_(0, 255),
        schema_(schema),
        instance_(schema_) {
    }

    // Non-blocking attempt to acquire one sample buffer.
    bool DoTryAcquireSample(SampleHandle& outHandle,
        const uint8_t*& outData,
        size_t& outSize,
        Sample& outMeta) override
    {
        return true;
    }
    // Blocking acquire with timeout: in this simple generator case we ignore timeout
    // and behave identical to TryAcquireSample (always returns immediately).
    bool DoAcquireSample(std::chrono::milliseconds /*timeout*/,
        SampleHandle& outHandle,
        const uint8_t*& outData,
        size_t& outSize,
        Sample& outMeta) override
    {
        // For an on-demand random generator there's no waiting: just produce synchronously.
        return false; // TryAcquireSample(outHandle, outData, outSize, outMeta);
    }

    // Release previously acquired buffer identified by handle.
    void DoReleaseSample(SampleHandle handle) override
    {
    }

protected:
    bool DoOnStart() override {
        return true;
    }

    void RunOnce() override {
        // Notify listeners
        PublishEvent({ "info", "random buffer produced", {} });
    }

private:
    static uint64_t currentTimeNs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // Simple RNG
    std::mt19937_64 gen_;
    std::uniform_int_distribution<int16_t> dist_;

    // Sequence counter
    std::atomic<uint64_t> seq_{ 0 };

    std::mutex activeMtx_;
    std::size_t lastHandleId_;

    Schema schema_;
    Instance instance_;
};