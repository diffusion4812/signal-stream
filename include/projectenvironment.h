#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <iostream>
#include <thread>
#include <condition_variable>

#include "service-bus.h"
#include "hash.h"
#include "project.h"
#include "storage-manager.h"
#include "stream-registry-impl.h"
#include "source.h"
#include "source-factory.h"
#include "schema.h"

// Deserialize Stream from json (throws std::runtime_error on invalid data)
inline SourceData parseSource(const json& j);

// Deserialize Project from json (throws std::runtime_error on invalid data)
inline ProjectData parseProject(const json& j);

bool LoadProjectFromFile(const std::string& path, ProjectData& outProject, std::string& outError);

// Project manager class: owns services and orchestrates lifecycle.
class ProjectManager {
public:
    using ErrorString = std::string;
    using SourcePtr = std::shared_ptr<ISource>;

    explicit ProjectManager() = default;

    ProjectManager(ServiceBus& bus, const std::string& path, bool autostart, boost::asio::io_context& ioc) :
            bus_(bus),
            path_(path),
            registry_(MakeSourceRegistry(bus_)),
            storage_(std::make_unique<StorageManager>()),
            ioc_(ioc) {

        storageSubscriptionToken_ = bus_.Subscribe<SourceRegistry::Event>([&](const SourceRegistry::Event& ev) {
                storage_->handle_registry_event(ev.type, ev.streamname, ev.meta);
            }
        );

        std::string err;
        storage_.get()->start();
        LoadProjectFromFile(path_, data_, err);
        LoadProject(data_, autostart, err);
        hash_ = fnv1a_32(path_);
    }

    ~ProjectManager() { StopAllServices(); }

    // Load project data (replace existing project). If 'autoStart' true, attempt to start services.
    // Returns true on success; on failure outError is filled.
    bool LoadProject(ProjectData pdata, bool autoStart, ErrorString& outError) {
        std::scoped_lock<std::mutex> lk(mtx_);

        // Stop and clear existing services if any
        StopAllServicesLocked();

        data_ = std::move(pdata);
        finaliseAllSchemas();

        // Pre-create service objects (but do not start them unless autoStart requested)
        for (const SourceData& desc : data_.sources) {
            if (sources_.contains(desc.name)) {
                outError = "duplicate stream name: " + desc.name;
                sources_.clear();
                return false;
            }

            // Build metadata for the registry
            StreamMetadata meta;
            meta.name = desc.name;
            meta.schema = desc.schema;

            // Create stream in registry (this will trigger StorageManager via event subscription)
            if (!registry_->create_stream(desc.name, meta)) {
                outError = "failed to register stream in registry: " + desc.name;
                sources_.clear();
                return false;
            }

            // Create source for this stream
            auto svc = CreateSourceByType(bus_, desc.name, desc.type, *desc.metadata.get(), desc.schema, *storage_.get(), ioc_);
            if (!svc) {
                outError = "no factory for service type: " + desc.type + " (stream: " + desc.name + ")";
                sources_.clear();
                return false;
            }

            sources_.emplace(desc.name, svc);
        }

        // Optionally start services
        if (autoStart) {
            //StartAllServicesLocked();
        }

        return true;
    }

    std::string GetName() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return data_.name;
    }

    std::string GetPath() const {
        return path_;
    }

    uint32_t GetHash() const {
        return hash_;
    }

    // Start all services (idempotent). Returns true on success.
    bool StartAllServices(ErrorString& outError) {
        std::scoped_lock<std::mutex> lk(mtx_);
        for (auto& kv : sources_) {
            try {
                if (kv.second->Status() == SourceStatus::Stopped || kv.second->Status() == SourceStatus::Error) {
                    kv.second->Start();
                    // Wait briefly for status to update, if needed
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
            catch (const std::exception& ex) {
                throw std::runtime_error("Failed to start service " + kv.first + ": " + ex.what());
            }
            if (kv.second->Status() != SourceStatus::Running) {
                throw std::runtime_error("Failed to start service " + kv.first);
            }
        }
        return true;
    }

    // Stop all services (idempotent)
    void StopAllServices() {
        std::scoped_lock<std::mutex> lk(mtx_);
        StopAllServicesLocked();
    }

    // Start a specific service by stream name
    bool StartService(const std::string& streamName, ErrorString& outError) {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = sources_.find(streamName);
        if (it == sources_.end()) {
            throw std::runtime_error("service not found: " + streamName);
        }
        try {
            it->second->Start();
        }
        catch (const std::exception& ex) {
            throw std::runtime_error("failed to start service " + streamName + ": " + ex.what());
        }
        return true;
    }

    // Stop a specific service
    bool StopService(const std::string& servicename, ErrorString& outError) {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = sources_.find(servicename);
        if (it == sources_.end()) { throw std::runtime_error("service not found: " + servicename); }
        try {
            it->second->Stop();
        }
        catch (const std::exception& ex) {
            throw std::runtime_error("failed to stop service " + servicename + ": " + ex.what());
        }
        return true;
    }

    // Get a shared_ptr to a service (caller may interact with buffer APIs etc).
    // Returns nullptr if not found.
    SourcePtr GetService(const std::string& servicename) const {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = sources_.find(servicename);
        return (it != sources_.end()) ? it->second : nullptr;
    }

    std::unordered_map<std::string, SourcePtr> GetAllServices() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return sources_;
    }

    // Query project metadata
    ProjectData GetProjectData() const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return data_;
    }

    StreamBufferHandle GetBufferHandle(const std::string& streamId) {
        std::scoped_lock lk(mtx_);
        auto handleOpt = storage_->GetBufferHandle(streamId);
        if (!handleOpt) {
            throw std::runtime_error("Stream not found: " + streamId);
        }
        return std::move(*handleOpt);
    }

    float GetBufferHealth(const std::string& servicename) const {
        std::scoped_lock<std::mutex> lk(mtx_);
        return storage_.get()->GetBufferHealth(servicename).value_or(0.0f);
    }

private:
    // internal helpers (assumes mtx_ locked where noted)
    void StopAllServicesLocked() {
        for (auto& kv : sources_) {
            try { kv.second->Stop(); }
            catch (...) {}
        }
        sources_.clear();
    }

    void finaliseAllSchemas() {
        for (auto& source : data_.sources) {
            if (!source.schema.isfinalised()) {
                source.schema.finalise();
            }
        }
    }

    ServiceBus& bus_;

    boost::asio::io_context& ioc_;

    mutable std::mutex mtx_;
    ProjectData data_;
    std::unordered_map<std::string, SourcePtr> sources_; // keyed by stream name

    std::string path_;
    uint32_t hash_ = 0;

    // Data storage and stream registry
    std::unique_ptr<SourceRegistry> registry_;
    std::unique_ptr<StorageManager> storage_;
    SubscriptionToken storageSubscriptionToken_; // Storage Manager subscription token for events published by Stream Registry
};