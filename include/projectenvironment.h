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

#include "project.h"
#include "service.h"
#include "servicefactory.h"
#include "schema.h"

// Lambda computes the Kind
Kind kind_from_str(std::string type_str) {
    if (type_str == "int32") return Kind::Int32;
    else if (type_str == "float64") return Kind::Float;
    else throw std::runtime_error("Unsupported type: " + type_str);  // Or return a default
    return Kind::Int32; // Default type
};

// Deserialize Stream from json (throws std::runtime_error on invalid data)
inline SourceData parseStream(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Stream entry is not an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Stream missing 'name' string");
    if (!j.contains("type") || !j["type"].is_string()) throw std::runtime_error("Stream missing 'type' string");
    if (!j.contains("signals") || !j["signals"].is_array()) throw std::runtime_error("Stream missing 'signals' array");

    SourceData stream;
    stream.name = j.at("name").get<std::string>();
    stream.type = j.at("type").get<std::string>();

    for (const auto& sig : j.at("signals")) {
        if (!sig.is_object()) throw std::runtime_error("Signal entry is not an object");
        if (!sig.contains("name") || !sig["name"].is_string()) throw std::runtime_error("Signal missing 'name' string");
        if (!sig.contains("type") || !sig["type"].is_string()) throw std::runtime_error("Signal missing 'type' string");
        if (!sig.contains("unit") || !sig["unit"].is_string()) throw std::runtime_error("Signal missing 'unit' string");

        stream.schema.add_field(sig.at("name").get<std::string>(), kind_from_str(sig.at("type").get<std::string>()));
    }
    return stream;
}

// Deserialize Project from json (throws std::runtime_error on invalid data)
inline ProjectData parseProject(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Root JSON must be an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Project missing 'name' string");
    if (!j.contains("streams") || !j["streams"].is_array()) throw std::runtime_error("Project missing 'streams' array");

    ProjectData p;
    p.name = j.at("name").get<std::string>();

    for (const auto& item : j.at("streams")) {
        p.sources.push_back(parseStream(item));
    }

    return p;
}

bool LoadProjectFromFile(const std::string& path, ProjectData& outProject, std::string& outError) {
    std::ifstream ifs(path);
    if (!ifs) {
        outError = "Failed to open file: " + path;
        return false;
    }

    try {
        json j;
        ifs >> j;
        outProject = parseProject(j);
        return true;
    }
    catch (const std::exception& ex) {
        outError = std::string("Error parsing project JSON: ") + ex.what();
        return false;
    }
}

// Project manager class: owns services and orchestrates lifecycle.
class ProjectManager {
public:
    using ErrorString = std::string;
    using ServicePtr = std::shared_ptr<IService>;
    using GlobalEventCallback = std::function<void(const SourceData&, const ServiceEvent&)>;

    explicit ProjectManager() = default;
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
            // attach a forwarding callback so ProjectManager can publish global events
            //AttachForwardingCallbackUnlocked(desc, svc);
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
            running_ = true;
        }
        else {
            running_ = false;
        }
        return true;
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
                outError = "Failed to start service " + kv.first + ": " + ex.what();
                running_ = false;  // Reset on failure
                return false;
            }
            if (kv.second->Status() != ServiceStatus::Running) {
                outError = "Failed to start service " + kv.first;
                running_ = false;
                return false;
            }
        }
        running_ = true;
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
        if (it == services_.end()) { outError = "service not found: " + streamName; return false; }
        try {
            it->second->Start();
        }
        catch (const std::exception& ex) {
            outError = "failed to start service " + streamName + ": " + ex.what();
            return false;
        }
        return true;
    }

    // Stop a specific service
    bool StopService(const std::string& streamName, ErrorString& outError) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = services_.find(streamName);
        if (it == services_.end()) { outError = "service not found: " + streamName; return false; }
        try {
            it->second->Stop();
        }
        catch (const std::exception& ex) {
            outError = "failed to stop service " + streamName + ": " + ex.what();
            return false;
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

    // Check if project currently has services running
    bool IsRunning() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return running_;
    }

private:
    // internal helpers (assumes mtx_ locked where noted)
    void StopAllServicesLocked() {
        for (auto& kv : services_) {
            try { kv.second->Stop(); }
            catch (...) {}
        }
        services_.clear();
        running_ = false;
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
    bool running_{ false };

    // global event callbacks: pair(id, callback)
    std::vector<std::pair<std::size_t, GlobalEventCallback>> globalCallbacks_;
    std::size_t nextGlobalCallbackId_{ 0 };
};