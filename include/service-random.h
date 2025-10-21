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
    static std::shared_ptr<RandomDataService> Create(const std::string& name, const Schema& schema, StorageManager& storage) {
        return std::make_shared<RandomDataService>(name, schema, storage);
    }

    RandomDataService(const std::string& name, const Schema& schema, StorageManager& storage)
        : ServiceBase(name, schema, storage),
        gen_(std::random_device{}()) {
    }

    // Non-blocking attempt to acquire one sample buffer.
    bool DoTryAcquireSample(SampleHandle& outHandle, Instance& instance) override
    {
        for (const auto& f : schema_.value().fields()) {
            switch (f.kind) {
                case Kind::Int32:
                    instance.set<int32_t>(f.name, static_cast<int32_t>(gen_()));
                    break;
                case Kind::Int64:
                    instance.set<int64_t>(f.name, static_cast<int64_t>(gen_()));
                    break;
                case Kind::Float:
                    instance.set<float>(f.name, static_cast<float>(gen_()));
                    break;
                case Kind::Double:
                    instance.set<double>(f.name, static_cast<double>(gen_()));
                    break;
            }
        }
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
        PublishEvent({ EventType::Information, "random buffer produced", {} });
        Instance instance(schema_.value());
        for (const auto& f : schema_.value().fields()) {
            switch (f.kind) {
            case Kind::Int32:
                instance.set<int32_t>(f.name, static_cast<int32_t>(rand_between(0, 100, gen_)));
                break;
            case Kind::Int64:
                instance.set<int64_t>(f.name, static_cast<int64_t>(gen_()));
                break;
            case Kind::Float:
                instance.set<float>(f.name, static_cast<float>(gen_()));
                break;
            case Kind::Double:
                instance.set<double>(f.name, static_cast<double>(gen_()));
                break;
            }
        }
        SubmitResult r = token_.try_submit(std::move(
            std::vector<uint8_t>(reinterpret_cast<uint8_t*>(
                instance.get_data()),
                reinterpret_cast<uint8_t*>(instance.get_data()) + schema_.value().instance_size())));
    }

private:
    template <typename T>
    T rand_between(T a, T b, std::mt19937_64& rng) {
        if constexpr (std::is_integral_v<T>) {
            // inclusive both ends for integers
            std::uniform_int_distribution<T> dist(a, b);
            return dist(rng);
        }
        else {
            // [a, b) for floating points
            std::uniform_real_distribution<T> dist(a, b);
            return dist(rng);
        }
    }

    // Simple RNG
    std::mt19937_64 gen_;

    // Sequence counter
    std::atomic<uint64_t> seq_{ 0 };

    std::mutex activeMtx_;
    std::size_t lastHandleId_;
};