#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex> // for reader-writer style if desired

#include "schema.h"
#include "storage-buffer.h"

// Forward declarations / placeholder types (replace with your real types)
struct StreamMetadata {
    std::string name;
    Schema schema;
};

struct StreamOptions {
    // e.g., start_auto, retention policy, etc.
    bool auto_start = true;
};

class StreamBuffer;

struct RegistryStreamHolder {
    StreamMetadata metadata;
    explicit RegistryStreamHolder(StreamMetadata m) : metadata(std::move(m)) {}
};

// Event types for registry notifications
enum class RegistryEventType {
    Created,
    Updated,
    Deleted,
    Renamed
};

// Simple alias for listener
using Listener = std::function<void(RegistryEventType, const std::string&, const StreamMetadata&)>;

// The StreamRegistry interface
class StreamRegistry {
public:
    // Create a new stream; returns true on success, false on conflict/invalid.
    virtual bool create_stream(const std::string& streamId, const StreamMetadata& meta) = 0;

    // Delete a stream; returns true on success.
    virtual bool delete_stream(const std::string& streamId) = 0;

    // Get a pointer/reference to a stream's holder (non-owning).
    virtual std::optional<std::shared_ptr<RegistryStreamHolder>> get_stream(const std::string& streamId) = 0;
    virtual std::optional<StreamMetadata> get_stream_metadata(const std::string& streamId) const = 0;

    // Update stream metadata; returns true on success.
    virtual bool update_stream(const std::string& streamId, const StreamMetadata& updatedMeta) = 0;

    // Rename a stream; returns true on success.
    virtual bool rename_stream(const std::string& oldName, const std::string& newName) = 0;

    // Listener subscription
    using Listener = std::function<void(RegistryEventType, const std::string&, const StreamMetadata&)>;
    virtual int register_listener(Listener listener) = 0;
    virtual void unregister_listener(int token) = 0;

    // Queries
    virtual std::vector<std::string> list_stream_ids() const = 0;
    virtual std::vector<StreamMetadata> list_stream_metadata() const = 0;

    // Reconciliation / startup
    virtual void reconcile_state() = 0;
    virtual void notify_of_external_change(const std::string& streamId) = 0;

    // Runtime helpers
    virtual size_t stream_count() const = 0;

    // Optional helpers to access runtime resources
    virtual std::shared_ptr<RegistryStreamHolder> get_or_create_holder(const std::string& streamId) = 0;

    // Registry-level locking (internal-use if needed)
    virtual void lock_registry() = 0;
    virtual void unlock_registry() = 0;

    // Subscription with token
    virtual std::shared_ptr<void> subscribe_with_token(Listener listener) = 0;

    // Publish an event (helper)
    virtual void publish_event(RegistryEventType type, const std::string& streamId, const StreamMetadata& meta) = 0;

    virtual ~StreamRegistry() = default;
};

// Factory convenience (optional)
std::unique_ptr<StreamRegistry> make_stream_registry();