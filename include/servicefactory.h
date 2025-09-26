#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>

#include "project.h"
#include "service.h" // IService, StreamDescriptor

// Factory function type: create service instance for given descriptor
using ServiceFactoryFn = std::function<std::shared_ptr<IService>(const SourceData&)>;

// Registry accessor. Constructed on first use (C++11+ thread-safe).
inline std::unordered_map<std::string, ServiceFactoryFn>& ServiceFactoryMap() {
    static std::unordered_map<std::string, ServiceFactoryFn> map;
    return map;
}

// Register a factory for a service type string. Returns true if inserted.
inline bool RegisterServiceFactory(const std::string& type, ServiceFactoryFn fn) {
    auto& m = ServiceFactoryMap();
    auto it = m.find(type);
    if (it != m.end()) return false; // already registered
    m.emplace(type, std::move(fn));
    return true;
}

// Registration macro that creates a unique static boolean to perform registration
// Usage:
//   REGISTER_SERVICE_TYPE("random", [](const StreamDescriptor& d){ return RandomDataService::Create(d); });
#define REGISTER_SERVICE_TYPE(TYPE_STR, FACTORY_EXPR)                       \
    namespace {                                                             \
        struct _service_registrar_##__LINE__ {                              \
            _service_registrar_##__LINE__() {                               \
                RegisterServiceFactory(TYPE_STR, FACTORY_EXPR);             \
            }                                                               \
        };                                                                  \
        static _service_registrar_##__LINE__ _service_registrar_instance_##__LINE__; \
    }

// Lookup helper: create service by type; returns nullptr if type not found.
inline std::shared_ptr<IService> CreateServiceByType(const std::string& type, const SourceData& desc) {
    auto& m = ServiceFactoryMap();
    auto it = m.find(type);
    if (it == m.end()) return nullptr;
    return it->second(desc);
}