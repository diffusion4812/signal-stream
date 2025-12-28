#pragma once

#include <vector>
#include <queue>
#include <random>
#include <atomic>
#include <thread>
#include <mutex>

#include "source.h"

class RandomSource : public Source {
public:
    struct Metadata : IMetadata {
    };

    // Create with a descriptor and a buffer capacity
    static std::shared_ptr<RandomSource> Create(ServiceBus& bus, const std::string& name, const Schema& schema, const Metadata& metadata, StorageManager& storage, boost::asio::io_context& ioc) {
        return std::make_shared<RandomSource>(bus, name, schema, metadata, storage, ioc);
    }

    RandomSource(ServiceBus& bus, const std::string& name, const Schema& schema, const Metadata& metadata, StorageManager& storage, boost::asio::io_context& ioc) :
        Source(bus, name, schema, storage, ioc),
        bus_(bus),
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
        const std::byte*& outData,
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
        Instance instance(schema_.value());

        for (const auto& f : schema_.value().fields()) {
            if (f.name == "_timestamp") {
                int64_t timestamp = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                instance.set<int64_t>("_timestamp", timestamp);
                continue;
            }
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

        const auto* begin = reinterpret_cast<const std::byte*>(instance.get_data());
        const auto* end = begin + schema_->instance_size();
        SubmitResult r = token_.try_submit(
            std::vector<std::byte>(begin, end)
        );

        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, std::chrono::milliseconds(20), [this] { return !running_.load(); });
    }

private:
    ServiceBus& bus_;

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
};