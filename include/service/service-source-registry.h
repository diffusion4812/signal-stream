#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

#include "schema.h"
#include "storage-buffer.h"
#include "service-bus.h"

struct SourceMetadata {
    std::string name;
    Schema schema;
};

struct SourceOptions {
    // e.g., start_auto, retention policy, etc.
    bool auto_start = true;
};

class StreamBuffer;

struct RegistrySourceHolder {
    SourceMetadata metadata;
    explicit RegistrySourceHolder(SourceMetadata m) : metadata(std::move(m)) {}
};

class SourceRegistry {
public:
    struct Event {
        enum class Type { Created, Updated, Deleted, Renamed };
        Type type;
        std::string streamname;
        const SourceMetadata& meta;
    };
    SourceRegistry(ServiceBus& bus);
    ~SourceRegistry();

    // Create a new stream; returns true on success, false on conflict/invalid.
    bool create_stream(const std::string& streamId, const SourceMetadata& meta);

    // Delete a stream; returns true on success.
    bool delete_stream(const std::string& streamId);

    // Get a pointer/reference to a stream's holder (non-owning).
    std::optional<std::shared_ptr<RegistrySourceHolder>> get_stream(const std::string& streamId);
    std::optional<SourceMetadata> get_stream_metadata(const std::string& streamId) const;

    // Update stream metadata; returns true on success.
    bool update_stream(const std::string& streamId, const SourceMetadata& updatedMeta);

    // Rename a stream; returns true on success.
    bool rename_stream(const std::string& oldName, const std::string& newName);

    // Queries
    std::vector<std::string> list_stream_ids() const;
    std::vector<SourceMetadata> list_stream_metadata() const;

    // Reconciliation / startup
    void reconcile_state();
    void notify_of_external_change(const std::string& streamId);

    // Runtime helpers
    size_t stream_count() const;

    // Optional helpers to access runtime resources
    std::shared_ptr<RegistrySourceHolder> get_or_create_holder(const std::string& streamId);

private:
    ServiceBus& bus_;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<RegistrySourceHolder>> holders_;
};