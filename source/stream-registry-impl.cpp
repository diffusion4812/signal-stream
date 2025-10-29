#include <algorithm>
#include <cassert>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <memory>

#include "stream-registry-impl.h"

SourceRegistryImpl::SourceRegistryImpl(ServiceBus& bus) :
    bus_(bus) {
}

SourceRegistryImpl::~SourceRegistryImpl() = default;

// Create stream
bool SourceRegistryImpl::create_stream(const std::string& streamId, const StreamMetadata& meta) {
    {
        std::scoped_lock lk(mtx_);
        if (holders_.contains(streamId)) {
            return true; // Already exists
        }
        holders_.emplace(streamId, std::make_shared<RegistryStreamHolder>(meta));
    }
    bus_.Publish<Event>(Event{ Event::Type::Created, streamId, meta });
    return true;
}

// Delete stream
bool SourceRegistryImpl::delete_stream(const std::string& streamId) {
    {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = holders_.find(streamId);
        if (it == holders_.end()) return false;

        // Cleanup runtime state if needed (buffers, files, etc.)
        holders_.erase(it);
    }
    bus_.Publish<Event>(Event{ Event::Type::Deleted, streamId, {} });
    return true;
}

// Get stream
std::optional<std::shared_ptr<RegistryStreamHolder>> SourceRegistryImpl::get_stream(const std::string& streamId) {
    std::scoped_lock<std::mutex> lk(mtx_);
    auto it = holders_.find(streamId);
    if (it == holders_.end()) return std::nullopt;
    return it->second;
}

// Get stream metadata
std::optional<StreamMetadata> SourceRegistryImpl::get_stream_metadata(const std::string& streamId) const {
    std::scoped_lock lk(mtx_);
    auto it = holders_.find(streamId);
    if (it == holders_.end()) return std::nullopt;
    return it->second->metadata;
}

// Update stream
bool SourceRegistryImpl::update_stream(const std::string& streamId, const StreamMetadata& updatedMeta) {
    {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = holders_.find(streamId);
        if (it == holders_.end()) return false;
    }
    bus_.Publish<Event>(Event{ Event::Type::Updated, streamId, updatedMeta });
    return true;
}

// Rename stream
bool SourceRegistryImpl::rename_stream(const std::string& oldName, const std::string& newName) {
    {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = holders_.find(oldName);
        if (it == holders_.end()) return false;

        auto holder = it->second;
        holders_.erase(it);
        holders_.emplace(newName, holder);
    }
    bus_.Publish<Event>(Event{ Event::Type::Renamed, newName, {} });
    return true;
}

// Lists
std::vector<std::string> SourceRegistryImpl::list_stream_ids() const {
    std::scoped_lock<std::mutex> lk(mtx_);
    std::vector<std::string> ids;
    ids.reserve(holders_.size());
    for (const auto& kv : holders_) ids.push_back(kv.first);
    return ids;
}

std::vector<StreamMetadata> SourceRegistryImpl::list_stream_metadata() const {
    std::scoped_lock<std::mutex> lk(mtx_);
    std::vector<StreamMetadata> meta;
    // Build from holders if you store metadata in StreamHolder
    // Placeholder: return empty
    return meta;
}

void SourceRegistryImpl::reconcile_state() {
    // Example: ensure runtime state aligns with registry
    // This is a no-op in this minimal prototype
}

void SourceRegistryImpl::notify_of_external_change(const std::string& streamId) {
    // Notify internal components if needed
    (void)streamId;
}

size_t SourceRegistryImpl::stream_count() const {
    std::scoped_lock<std::mutex> lk(mtx_);
    return holders_.size();
}

std::shared_ptr<RegistryStreamHolder> SourceRegistryImpl::get_or_create_holder(const std::string& streamId) {
    std::scoped_lock<std::mutex> lk(mtx_);
    auto it = holders_.find(streamId);
    if (it != holders_.end()) return it->second;
    // Create a minimal holder if desired (requires appropriate defaults)
    // For this prototype, return nullptr to signal not found.
    return nullptr;
}

std::unique_ptr<SourceRegistry> MakeSourceRegistry(ServiceBus& bus) {
    return std::make_unique<SourceRegistryImpl>(bus);
}