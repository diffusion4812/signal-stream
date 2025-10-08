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
#include <iostream>  // For logging; replace with your library

#include "project.h"
#include "schema.h"  // Include the schema class
#include "instance.h" // Include the instance class for schema integration

// Simple status enum for services.
enum class ServiceStatus {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error
};

// Struct for service events.
struct ServiceEvent {
    std::string eventType;
    std::string message;
    std::optional<std::string> payload;
};

struct Sample {
    uint64_t timestampNs;
    uint64_t seq;
    uint32_t flags;
    Instance instance; // Use Instance to hold schema-based data
};

using SampleHandle = std::uintptr_t;

class LoopbackCallback {
    public:
        LoopbackCallback() {}
        void Publish(const ServiceEvent& ev) {
            event_ = std::move(ev);
        }

    private:
        ServiceEvent event_;
};

// Abstract interface for services.
class IService {
public:
    virtual ~IService() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual ServiceStatus Status() const = 0;
    virtual const SourceData& Source() const = 0;

    virtual bool SetupSchema(const Schema& schema) = 0;
    using ServiceCallback = std::function<void(const ServiceEvent&)>;
    virtual std::size_t RegisterCallback(ServiceCallback cb) = 0;
    LoopbackCallback loopbackCallback;
    virtual void UnregisterCallback(std::size_t handle) = 0;

    virtual bool TryAcquireSample(SampleHandle& outHandle, Instance& instance) = 0;
    virtual bool AcquireSample(std::chrono::milliseconds timeout, SampleHandle& outHandle, const uint8_t*& outData, size_t& outSize, Sample& outMeta) = 0;
    virtual void ReleaseSample(SampleHandle handle) = 0;
};

// Concrete base class with schema integration.
class ServiceBase : public IService {
public:
    explicit ServiceBase(const Schema& schema)
        :
        status_(ServiceStatus::Stopped),
        nextCallbackHandle_(1),
        running_(false),
        schema_(schema) {
    }

    virtual ~ServiceBase() { Stop(); }

    void Start() override {
        ServiceStatus expected = ServiceStatus::Stopped;
        if (!status_.compare_exchange_strong(expected, ServiceStatus::Starting)) {
            std::cout << "[LOG] Service already starting or running." << std::endl;
            return;
        }
        try {
            if (!OnStart()) {
                status_.store(ServiceStatus::Error);
                PublishEvent({ "Error", "Failed to start service", std::nullopt });
                return;
            }
            running_.store(true);
            worker_ = std::jthread([this] { RunLoop(); });
            status_.store(ServiceStatus::Running);
            PublishEvent({ "Info", "Service started successfully", std::nullopt });
        }
        catch (const std::exception& e) {
            status_.store(ServiceStatus::Error);
            PublishEvent({ "Error", std::string("Exception during start: ") + e.what(), std::nullopt });
        }
    }

    void Stop() override {
        auto currentStatus = status_.load();
        if (currentStatus == ServiceStatus::Stopped || currentStatus == ServiceStatus::Stopping) return;
        status_.store(ServiceStatus::Stopping);
        running_.store(false);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            cv_.notify_all();
        }
        if (worker_.joinable()) worker_.join();
        OnStop();
        status_.store(ServiceStatus::Stopped);
        PublishEvent({ "Info", "Service stopped", std::nullopt });
    }

    ServiceStatus Status() const override { return status_.load(); }
    const SourceData& Source() const override { return source_; }

    bool SetupSchema(const Schema& schema) {
        if (status_.load() != ServiceStatus::Stopped) {
            PublishEvent({ "Warning", "Cannot setup schema: Service not stopped", std::nullopt });
            return false;
        }
        schema_ = schema;
        if (!schema_->isfinalised()) { // Schema must be finalised to have access to instance data (size etc.)
            PublishEvent({ "Warning", "Cannot setup schema: Schema not finalised", std::nullopt });
            return false;
        }
        PublishEvent({ "Info", "Schema setup completed", std::nullopt });
        return true;
    }

    std::size_t RegisterCallback(ServiceCallback cb) override {
        if (!cb) {
            std::cout << "[LOG] Invalid callback provided." << std::endl;
            return 0;
        }
        std::lock_guard<std::recursive_mutex> lk(cbMtx_);
        std::size_t h = nextCallbackHandle_++;
        callbacks_.emplace_back(h, std::move(cb));
        return h;
    }

    void UnregisterCallback(std::size_t handle) override {
        std::lock_guard<std::recursive_mutex> lk(cbMtx_);
        auto it = std::remove_if(callbacks_.begin(), callbacks_.end(),
            [handle](auto& p) { return p.first == handle; });
        if (it != callbacks_.end()) {
            callbacks_.erase(it, callbacks_.end());
        }
        else {
            std::cout << "[LOG] Callback handle not found." << std::endl;
        }
    }

    virtual bool TryAcquireSample(SampleHandle& outHandle, Instance& instance) override {
        bool result = DoTryAcquireSample(outHandle, instance);
        return result;
    }

    virtual bool AcquireSample(std::chrono::milliseconds timeout, SampleHandle& outHandle, const uint8_t*& outData, size_t& outSize, Sample& outMeta) override {
        //bool result = DoAcquireSample(timeout, outHandle, outData, outSize, outMeta);
        bool result = false;
        if (result && schema_->isfinalised() && outSize != schema_->instance_size()) {
            PublishEvent({ "Warning", "Sample size does not match schema", std::nullopt });
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

    void PublishEvent(const ServiceEvent& ev) {
        std::lock_guard<std::recursive_mutex> lk(cbMtx_);
        // Copy callbacks to avoid modification during iteration
        auto callbacksCopy = callbacks_;

        for (auto& p : callbacksCopy) {
            try {
                p.second(ev);
            }
            catch (const std::exception& e) {
                std::cout << "[LOG] Exception in callback [" << p.first << " - " << "]: " << e.what() << std::endl;
            }
        }
    }

    void RunLoop() {
        while (running_.load()) {
            try {
                RunOnce();
            }
            catch (const std::exception& e) {
                PublishEvent({ "Error", std::string("Exception in RunOnce: ") + e.what(), std::nullopt });
            }
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(200), [this] { return !running_.load(); });
        }
    }

    virtual void RunOnce() {}

    // Pure virtual
    virtual bool DoOnStart() = 0;
    virtual bool DoTryAcquireSample(SampleHandle&, Instance&) = 0;
    virtual bool DoAcquireSample(std::chrono::milliseconds, SampleHandle&, const uint8_t*&, size_t&, Sample&) = 0;
    virtual void DoReleaseSample(SampleHandle) = 0;

    SourceData source_;
    std::optional<Schema> schema_; // Integrated schema
    LoopbackCallback loopbackCallback_;

private:
    std::recursive_mutex cbMtx_;
    std::vector<std::pair<std::size_t, ServiceCallback>> callbacks_;

    std::size_t nextCallbackHandle_;

    std::atomic<bool> running_;
    std::jthread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<ServiceStatus> status_;
};