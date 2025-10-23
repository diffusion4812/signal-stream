#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>

#include "project.h"
#include "source.h" // IService, StreamDescriptor

// Factory function type: create service instance for given descriptor
using SourceFactoryFn = std::function<std::shared_ptr<ISource>(const std::string& name, const Schema& schema, StorageManager& storage)>;

// Registry accessor. Constructed on first use (C++11+ thread-safe).
inline std::unordered_map<std::string, SourceFactoryFn>& SourceFactoryMap() {
    static std::unordered_map<std::string, SourceFactoryFn> map;
    return map;
}

// Register a factory for a service type string. Returns true if inserted.
inline bool RegisterSourceFactory(const std::string& type, SourceFactoryFn fn) {
    auto& m = SourceFactoryMap();
    auto it = m.find(type);
    if (it != m.end()) return false; // already registered
    m.emplace(type, std::move(fn));
    return true;
}

// Registration macro that creates a unique static boolean to perform registration
// Usage:
//   REGISTER_SOURCE_TYPE("random", [](std::string type, const Schema& schema){ return RandomDataService::Create(type, schema); });
#define REGISTER_SOURCE_TYPE(TYPE_STR, FACTORY_EXPR)                       \
    namespace {                                                             \
        struct _source_registrar_##__LINE__ {                              \
            _source_registrar_##__LINE__() {                               \
                RegisterSourceFactory(TYPE_STR, FACTORY_EXPR);             \
            }                                                               \
        };                                                                  \
        static _source_registrar_##__LINE__ _source_registrar_instance_##__LINE__; \
    }

// Lookup helper: create source by type; returns nullptr if type not found.
inline std::shared_ptr<ISource> CreateSourceByType(const std::string& name, const std::string& type, const Schema& schema, StorageManager& storage) {
    auto& m = SourceFactoryMap();
    auto it = m.find(type);
    if (it == m.end()) return nullptr;
    return it->second(name, schema, storage);
}