#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>

#include "project.h"
#include "source.h"

// Factory function type: create service instance for given descriptor
using SourceFactoryFn = std::function<std::shared_ptr<ISource>(const std::string& name, const Schema& schema, const IMetadata& metadata, StorageManager& storage, boost::asio::io_context& ioc)>;

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
#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

#define REGISTER_SOURCE_TYPE_WITH_META(TYPE_STR, SOURCE_CLASS) \
    namespace { \
        struct CONCAT(_source_registrar, __LINE__) { \
            CONCAT(_source_registrar, __LINE__)() { \
                RegisterSourceFactory(TYPE_STR, \
                    [](const std::string& name, \
                       const Schema& schema, \
                       const IMetadata& metadata, \
                       StorageManager& storage, \
                       boost::asio::io_context& ioc) \
                    { \
                        auto const& typedMeta = dynamic_cast<const SOURCE_CLASS::Metadata&>(metadata); \
                        return SOURCE_CLASS::Create(name, schema, typedMeta, storage, ioc); \
                    } \
                ); \
            } \
        }; \
        static CONCAT(_source_registrar, __LINE__) CONCAT(_source_registrar_instance, __LINE__); \
    }

// Lookup helper: create source by type; returns nullptr if type not found.
inline std::shared_ptr<ISource> CreateSourceByType(const std::string& name, const std::string& type, const IMetadata& metadata, const Schema& schema, StorageManager& storage, boost::asio::io_context& ioc) {
    auto& m = SourceFactoryMap();
    auto it = m.find(type);
    if (it == m.end()) return nullptr;
    return it->second(name, schema, metadata, storage, ioc);
}