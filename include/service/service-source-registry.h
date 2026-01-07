#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <memory>
#include <optional>

#include "service-bus.h"

namespace signal_stream {

    // Simplified registry - names only
    class SourceRegistry {
    public:
        // Lightweight events - no metadata needed
        struct Event {
            enum class Type {
                Registered,
                Unregistered,
                Renamed
            };

            Type type;
            std::string source_name;
            std::optional<std::string> old_name;  // Only for Renamed events
        };

        explicit SourceRegistry(ServiceBus& bus);
        ~SourceRegistry();

        // Core operations
        bool register_source(const std::string& name);
        bool unregister_source(const std::string& name);
        bool rename_source(const std::string& oldName, const std::string& newName);

        // Query operations
        bool is_registered(const std::string& name) const;
        std::vector<std::string> list_all_sources() const;
        size_t count() const;

        // Utility
        void clear();

    private:
        ServiceBus& bus_;
        mutable std::mutex mtx_;
        std::unordered_set<std::string> source_names_;
    };

}