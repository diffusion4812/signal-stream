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

#include "hash.h"
#include "project.h"
#include "service.h"
#include "servicefactory.h"
#include "schema.h"

// Deserialize Stream from json (throws std::runtime_error on invalid data)
inline SourceData parseStream(const json& j);

// Deserialize Project from json (throws std::runtime_error on invalid data)
inline ProjectData parseProject(const json& j);

bool LoadProjectFromFile(const std::string& path, ProjectData& outProject, std::string& outError);

// Project manager class: owns services and orchestrates lifecycle.
class ProjectManager {
public:
    using ErrorString = std::string;
    using ServicePtr = std::shared_ptr<IService>;
    using GlobalEventCallback = std::function<void(const SourceData&, const ServiceEvent&)>;

    explicit ProjectManager() = default;

    ProjectManager(std::string path, bool autostart) : path_(path) {
        std::string err;
        LoadProjectFromFile(path_, data_, err);
        LoadProject(data_, autostart, err);
        hash_ = fnv1a_32(path_);
    }

    ~ProjectManager() { StopAllServices(); }

    // Load project data (replace existing project). If 'autoStart' true, attempt to start services.
    // Returns true on success; on failure outError is filled.
    bool LoadProject(ProjectData pdata, bool autoStart, ErrorString& outError) {
        std::lock_guard<std::mutex> lk(mtx_);

        // Stop and clear existing services if any
        StopAllServicesLocked();

        data_ = std::move(pdata);
        finaliseAllSchemas();

        // Pre-create service objects (but do not start them unless autoStart requested)
        for (const auto& desc : data_.sources) {
            if (services_.count(desc.name)) {
                // duplicate stream name in project — treat as error
                outError = "duplicate stream name: " + desc.name;
                services_.clear();
                return false;
            }
            auto svc = CreateServiceByType(desc.type, desc.schema);
            if (!svc) {
                outError = "no factory for service type: " + desc.type + " (stream: " + desc.name + ")";
                services_.clear();
                return false;
            }
            services_.emplace(desc.name, svc);
        }

        if (autoStart) {
            // start all services; collect possible failures (factory succeeded so start shouldn't fail catastrophically)
            for (auto& kv : services_) {
                try {
                    kv.second->Start();
                }
                catch (const std::exception& ex) {
                    outError = std::string("failed to start service ") + kv.first + ": " + ex.what();
                    // attempt to stop already-started services
                    StopAllServicesLocked();
                    return false;
                }
            }
        }
        return true;
    }

    std::string GetName() const {
        std::lock_guard<std::mutex> lk(mtx_);
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
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& kv : services_) {
            try {
                if (kv.second->Status() == ServiceStatus::Stopped || kv.second->Status() == ServiceStatus::Error) {
                    kv.second->Start();
                    // Wait briefly for status to update, if needed
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
            catch (const std::exception& ex) {
                throw std::runtime_error("Failed to start service " + kv.first + ": " + ex.what());
            }
            if (kv.second->Status() != ServiceStatus::Running) {
                throw std::runtime_error("Failed to start service " + kv.first);
            }
        }
        return true;
    }

    // Stop all services (idempotent)
    void StopAllServices() {
        std::lock_guard<std::mutex> lk(mtx_);
        StopAllServicesLocked();
    }

    // Start a specific service by stream name
    bool StartService(const std::string& streamName, ErrorString& outError) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = services_.find(streamName);
        if (it == services_.end()) { throw std::runtime_error("service not found: " + streamName); }
        try {
            it->second->Start();
        }
        catch (const std::exception& ex) {
            throw std::runtime_error("failed to start service " + streamName + ": " + ex.what());
        }
        return true;
    }

    // Stop a specific service
    bool StopService(const std::string& streamName, ErrorString& outError) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = services_.find(streamName);
        if (it == services_.end()) { throw std::runtime_error("service not found: " + streamName); }
        try {
            it->second->Stop();
        }
        catch (const std::exception& ex) {
            throw std::runtime_error("failed to stop service " + streamName + ": " + ex.what());
        }
        return true;
    }

    // Get a shared_ptr to a service (caller may interact with buffer APIs etc).
    // Returns nullptr if not found.
    ServicePtr GetService(const std::string& servicename) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = services_.find(servicename);
        return (it != services_.end()) ? it->second : nullptr;
    }

    std::unordered_map<std::string, ServicePtr> GetAllServices() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return services_;
    }

    // Query project metadata
    ProjectData GetProjectData() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return data_;
    }

    // Register a global event callback invoked when any service emits an event.
    // Multiple registrations supported; returns an id for later deregistration.
    std::size_t RegisterGlobalEventCallback(GlobalEventCallback cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        std::size_t id = ++nextGlobalCallbackId_;
        globalCallbacks_.emplace_back(id, std::move(cb));
        return id;
    }

    // Unregister global event callback
    void UnregisterGlobalEventCallback(std::size_t id) {
        std::lock_guard<std::mutex> lk(mtx_);
        globalCallbacks_.erase(
            std::remove_if(globalCallbacks_.begin(), globalCallbacks_.end(),
                [id](auto& p) { return p.first == id; }), globalCallbacks_.end());
    }

private:
    // internal helpers (assumes mtx_ locked where noted)
    void StopAllServicesLocked() {
        for (auto& kv : services_) {
            try { kv.second->Stop(); }
            catch (...) {}
        }
        services_.clear();
    }

    void finaliseAllSchemas() {
        for (auto& source : data_.sources) {
            if (!source.schema.isfinalised()) {
                source.schema.finalise();
            }
        }
    }

    // Attach a per-service callback that forwards to global callbacks.
    // Must be called while holding mtx_
    void AttachForwardingCallbackUnlocked(const SourceData& desc, ServicePtr svc) {
        auto forwarder = [this, desc](const ServiceEvent& ev) {
            // copy callbacks under lock then invoke outside lock to avoid deadlocks
            std::vector<GlobalEventCallback> cbs;
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto const& p : globalCallbacks_) cbs.push_back(p.second); // Create a local copy of callbacks
            for (auto const& cb : cbs) {
                try { cb(desc, ev); }
                catch (...) { /* swallow */ }
            }
            };
        // register callback on the service
        svc->RegisterCallback([forwarder](const ServiceEvent& ev) { forwarder(ev); });
    }

    mutable std::mutex mtx_;
    ProjectData data_;
    std::unordered_map<std::string, ServicePtr> services_; // keyed by stream name

    std::string path_;
    uint32_t hash_ = 0;

    // global event callbacks: pair(id, callback)
    std::vector<std::pair<std::size_t, GlobalEventCallback>> globalCallbacks_;
    std::size_t nextGlobalCallbackId_{ 0 };
};