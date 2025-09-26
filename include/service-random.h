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
    static std::shared_ptr<RandomDataService> Create(SourceData desc, size_t bufferCount = 8) {
        return std::make_shared<RandomDataService>(std::move(desc), bufferCount);
    }

    RandomDataService(SourceData desc, size_t bufferCount)
        : ServiceBase(std::move(desc)),
        bufferCount_(bufferCount),
        gen_(std::random_device{}()),
        dist_(0, 255),
        format_{ ElementType::INT16, 1, 256, 0, Endianness::Little } {
        // Pre-allocate ring buffers
        ring_.reserve(bufferCount_);
        for (size_t i = 0; i < bufferCount_; ++i) {
            ring_.emplace_back(std::vector<uint8_t>(format_.samplesPerChannel * elementSize(format_.elementType) * format_.channels, 0));
            ringMeta_.push_back(SampleMetadata{ 0, 0, 0, format_ });
        }
    }

    // Non-blocking attempt to acquire one sample buffer.
    bool TryAcquireSample(SampleHandle& outHandle,
        const uint8_t*& outData,
        size_t& outSize,
        SampleMetadata& outMeta) override
    {
        // Generate one buffer immediately and return it.
        size_t bufSize = static_cast<size_t>(format_.samplesPerChannel) *
            static_cast<size_t>(format_.channels) *
            elementSize(format_.elementType);

        // Fill buffer with random bytes
        auto buf = std::make_shared<std::vector<uint8_t>>(bufSize);
        for (size_t i = 0; i < bufSize; ++i) {
            (*buf)[i] = static_cast<uint8_t>(dist_(gen_));
        }

        SampleMetadata meta;
        meta.timestampNs = currentTimeNs();
        meta.seq = seq_.fetch_add(1);
        meta.flags = 0;
        meta.format = format_;

        // Wrap in container that will be stored until ReleaseSample is called
        auto container = std::make_shared<BufferContainer>(std::move(*buf), meta);

        // Register active handle
        std::size_t id = ++lastHandleId_;
        {
            std::lock_guard<std::mutex> lg(activeMtx_);
            activeHandles_.emplace(id, container);
        }

        outHandle = static_cast<SampleHandle>(id);
        outData = container->data().data();
        outSize = container->data().size();
        outMeta = container->meta();
        return true;
    }
    // Blocking acquire with timeout: in this simple generator case we ignore timeout
    // and behave identical to TryAcquireSample (always returns immediately).
    bool AcquireSample(std::chrono::milliseconds /*timeout*/,
        SampleHandle& outHandle,
        const uint8_t*& outData,
        size_t& outSize,
        SampleMetadata& outMeta) override
    {
        // For an on-demand random generator there's no waiting: just produce synchronously.
        return TryAcquireSample(outHandle, outData, outSize, outMeta);
    }

    // Release previously acquired buffer identified by handle.
    void ReleaseSample(SampleHandle handle) override
    {
        std::size_t id = static_cast<std::size_t>(handle);
        std::lock_guard<std::mutex> lg(activeMtx_);
        auto it = activeHandles_.find(id);
        if (it != activeHandles_.end()) {
            activeHandles_.erase(it); // shared_ptr destroyed -> buffer freed
        }
    }

    // Implement required interface
    SampleFormat GetSampleFormat() const override {
        return format_;
    }

protected:
    // Start/Stop hooks
    bool OnStart() override {
        return true;
    }

    // Core loop: produce random data periodically
    void RunOnce() override {
        // Produce one buffer worth of data if there is space available
        if (ring_.size() >= maxBuffers_) return;

        // Produce deterministic-ish random bytes
        std::vector <uint8_t> buf(format_.samplesPerChannel * format_.channels);
        for (size_t i = 0; i < buf.size(); ++i) {
            buf[i] = static_cast<int16_t>(dist_(gen_));
        }

        SampleBufferInfo info;
        info.data = buf.data();
        info.size = buf.size();
        info.meta.timestampNs = currentTimeNs();
        info.meta.seq = seq_++;
        info.meta.format = format_;

        {
            std::lock_guard<std::mutex> lg(ringMtx_);
            ring_.push_back(std::move(buf));
            ringMeta_.push_back(info.meta);
        }

        // Notify listeners
        PublishEvent({ "info", "random buffer produced", {} });
    }

private:
    // Helpers
    struct BufferContainer {
        BufferContainer(std::vector<uint8_t>&& d, SampleMetadata m)
            : data_(std::move(d)), meta_(m) {
        }
        const std::vector<uint8_t>& data() const { return data_; }
        const SampleMetadata& meta() const { return meta_; }
        std::vector<uint8_t> data_;
        SampleMetadata meta_;
    };

    static uint64_t currentTimeNs() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static size_t elementSize(ElementType ft) {
        switch (ft) {
        case ElementType::UINT8:  return 1;
        case ElementType::INT16:  return 2;
        case ElementType::INT32:  return 4;
        case ElementType::FLOAT32: return 4;
        case ElementType::FLOAT64: return 8;
        }
        return 1;
    }

    // Internal ring buffer
    std::vector < std::vector <uint8_t>> ring_;
    std::vector<SampleMetadata> ringMeta_;
    std::mutex ringMtx_;
    size_t bufferCount_;
    size_t maxBuffers_ = 16; // soft cap to avoid unbounded growth

    // Simple RNG
    std::mt19937_64 gen_;
    std::uniform_int_distribution<int16_t> dist_;

    // Sequence counter
    std::atomic<uint64_t> seq_{ 0 };

    // Signal format
    SampleFormat format_;

    std::mutex activeMtx_;
    std::unordered_map<std::size_t, std::shared_ptr<BufferContainer>> activeHandles_;
    std::size_t lastHandleId_;
};