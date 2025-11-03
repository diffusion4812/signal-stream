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
#include <SDL3/SDL_log.h>
#include <boost/asio.hpp>

#include "service-bus.h"
#include "service-logger.h"
#include "service-storage.h"
#include "project.h"
#include "schema.h"  // Include the schema class
#include "instance.h" // Include the instance class for schema integration

enum class SourceStatus {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error
};

struct Sample {
    uint64_t timestampNs;
    uint64_t seq;
    uint32_t flags;
    Instance instance; // Use Instance to hold schema-based data
};

using SampleHandle = std::uintptr_t;

// Abstract interface for services.
class ISource {
public:
    virtual ~ISource() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual SourceStatus Status() const = 0;

    virtual bool TryAcquireSample(SampleHandle& outHandle, Instance& instance) = 0;
    virtual bool AcquireSample(std::chrono::milliseconds timeout, SampleHandle& outHandle, const std::byte*& outData, size_t& outSize, Sample& outMeta) = 0;
    virtual void ReleaseSample(SampleHandle handle) = 0;
};

// Concrete base class with schema integration.
class Source : public ISource {
public:
    struct Event {
        enum class Type { None, Information, Notification, Warning, Alarm, Critical };
        Type type;
        std::string message;
        std::optional<std::string> payload;
    };

    explicit Source(ServiceBus& bus, const std::string& name, const Schema& schema, StorageManager& storage, boost::asio::io_context& ioc) :
        bus_(bus),
        status_(SourceStatus::Stopped),
        running_(false),
        schema_(schema),
        lastEvent_(Event::Type::None, "") {
        bus_.Subscribe<Event>([&](const Event& ev) {
            lastEvent_ = ev;
        });
        std::optional<ProducerToken> token = storage.get_producer_token(name);
        if (!token) {
            throw::std::runtime_error("Unable to obtain producer token: " + name);
        }
        token_ = token.value();
    }

    virtual ~Source() { Stop(); }

    void Start() override {
        SourceStatus expected = SourceStatus::Stopped;
        if (!status_.compare_exchange_strong(expected, SourceStatus::Starting)) {
            std::cout << "[LOG] Service already starting or running." << std::endl;
            return;
        }
        try {
            if (!OnStart()) {
                status_.store(SourceStatus::Error);
                bus_.Publish<Event>(Event{ Event::Type::Critical, "Failed to start service", std::nullopt });
                return;
            }
            running_.store(true);
            worker_ = std::jthread([this] { RunLoop(); });
            status_.store(SourceStatus::Running);
            bus_.Publish<Event>(Event{ Event::Type::Notification, "Service started successfully", std::nullopt });
        }
        catch (const std::exception& e) {
            status_.store(SourceStatus::Error);
            bus_.Publish<Event>(Event{ Event::Type::Critical, std::string("Exception during start: ") + e.what(), std::nullopt });
        }
    }

    void Stop() override {
        auto currentStatus = status_.load();
        if (currentStatus == SourceStatus::Stopped || currentStatus == SourceStatus::Stopping) return;
        status_.store(SourceStatus::Stopping);
        running_.store(false);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            cv_.notify_all();
        }
        if (worker_.joinable()) worker_.join();
        OnStop();
        status_.store(SourceStatus::Stopped);
        bus_.Publish<Event>(Event{ Event::Type::Notification, "Service stopped", std::nullopt });
    }

    SourceStatus Status() const override { return status_.load(); }

    Event* GetLastEvent() {
        return &lastEvent_;
    }

    virtual bool TryAcquireSample(SampleHandle& outHandle, Instance& instance) override {
        if (!running_.load()) return false;
        bool result = DoTryAcquireSample(outHandle, instance);
        return result;
    }

    virtual bool AcquireSample(std::chrono::milliseconds timeout, SampleHandle& outHandle, const std::byte*& outData, size_t& outSize, Sample& outMeta) override {
        //bool result = DoAcquireSample(timeout, outHandle, outData, outSize, outMeta);
        bool result = false;
        if (result && schema_->isfinalised() && outSize != schema_->instance_size()) {
            bus_.Publish<Event>(Event{ Event::Type::Notification, "Sample size does not match schema", std::nullopt });
        }
        return result;
    }

    /**
     * Release sample stored in service implemented buffer - identified by handle.
     */
    virtual void ReleaseSample(SampleHandle handle) override {
        DoReleaseSample(handle);
    }

protected:
    virtual bool OnStart() {
        if (!schema_.has_value()) return false;
        if (!schema_->isfinalised()) return false;
        return DoOnStart();
    }

    virtual bool OnStop() {
        return true;
    }

    void RunLoop() {
        while (running_.load()) {
            try {
                RunOnce();
            }
            catch (const std::exception& e) {
                bus_.Publish<Logger::Event>(Logger::Event{ Logger::Event::Severity::Critical, std::string("Exception in RunOnce: ") + e.what() });
            }
        }
    }

    virtual void RunOnce() {}

    // Pure virtual
    virtual bool DoOnStart() = 0;
    virtual bool DoTryAcquireSample(SampleHandle&, Instance&) = 0;
    virtual bool DoAcquireSample(std::chrono::milliseconds, SampleHandle&, const std::byte*&, size_t&, Sample&) = 0;
    virtual void DoReleaseSample(SampleHandle) = 0;

    std::optional<Schema> schema_;

private:
    ServiceBus& bus_;
    Event lastEvent_;

    std::jthread worker_;
    std::atomic<SourceStatus> status_;

protected:
    ProducerToken token_;
    std::atomic<bool> running_;
    std::mutex mtx_;
    std::condition_variable cv_;
};