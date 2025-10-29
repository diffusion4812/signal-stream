#include <algorithm>
#include <cassert>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <memory>

#include "stream-registry-impl.h"

StreamRegistryImpl::StreamRegistryImpl(ServiceBus& bus) :
    bus_(bus) {
}

StreamRegistryImpl::~StreamRegistryImpl() = default;

// Create stream
bool StreamRegistryImpl::create_stream(const std::string& streamId, const StreamMetadata& meta) {
    {
        std::scoped_lock lk(mtx_);
        if (holders_.contains(streamId)) {
            return true; // Already exists
        }
        holders_.emplace(streamId, std::make_shared<RegistryStreamHolder>(meta));
    }
    bus_.Publish<RegistryEvent>(RegistryEvent{ RegistryEvent::Type::Created, streamId, meta });
    return true;
}

// Delete stream
bool StreamRegistryImpl::delete_stream(const std::string& streamId) {
    {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = holders_.find(streamId);
        if (it == holders_.end()) return false;

        // Cleanup runtime state if needed (buffers, files, etc.)
        holders_.erase(it);
    }
    bus_.Publish<RegistryEvent>(RegistryEvent{ RegistryEvent::Type::Deleted, streamId, {} });
    return true;
}

// Get stream
std::optional<std::shared_ptr<RegistryStreamHolder>> StreamRegistryImpl::get_stream(const std::string& streamId) {
    std::scoped_lock<std::mutex> lk(mtx_);
    auto it = holders_.find(streamId);
    if (it == holders_.end()) return std::nullopt;
    return it->second;
}

// Get stream metadata
std::optional<StreamMetadata> StreamRegistryImpl::get_stream_metadata(const std::string& streamId) const {
    std::scoped_lock lk(mtx_);
    auto it = holders_.find(streamId);
    if (it == holders_.end()) return std::nullopt;
    return it->second->metadata;
}

// Update stream
bool StreamRegistryImpl::update_stream(const std::string& streamId, const StreamMetadata& updatedMeta) {
    {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = holders_.find(streamId);
        if (it == holders_.end()) return false;
    }
    bus_.Publish<RegistryEvent>(RegistryEvent{ RegistryEvent::Type::Updated, streamId, updatedMeta });
    return true;
}

// Rename stream
bool StreamRegistryImpl::rename_stream(const std::string& oldName, const std::string& newName) {
    {
        std::scoped_lock<std::mutex> lk(mtx_);
        auto it = holders_.find(oldName);
        if (it == holders_.end()) return false;

        auto holder = it->second;
        holders_.erase(it);
        holders_.emplace(newName, holder);
    }
    bus_.Publish<RegistryEvent>(RegistryEvent{ RegistryEvent::Type::Renamed, newName, {} });
    return true;
}

// Lists
std::vector<std::string> StreamRegistryImpl::list_stream_ids() const {
    std::scoped_lock<std::mutex> lk(mtx_);
    std::vector<std::string> ids;
    ids.reserve(holders_.size());
    for (const auto& kv : holders_) ids.push_back(kv.first);
    return ids;
}

std::vector<StreamMetadata> StreamRegistryImpl::list_stream_metadata() const {
    std::scoped_lock<std::mutex> lk(mtx_);
    std::vector<StreamMetadata> meta;
    // Build from holders if you store metadata in StreamHolder
    // Placeholder: return empty
    return meta;
}

void StreamRegistryImpl::reconcile_state() {
    // Example: ensure runtime state aligns with registry
    // This is a no-op in this minimal prototype
}

void StreamRegistryImpl::notify_of_external_change(const std::string& streamId) {
    // Notify internal components if needed
    (void)streamId;
}

size_t StreamRegistryImpl::stream_count() const {
    std::scoped_lock<std::mutex> lk(mtx_);
    return holders_.size();
}

std::shared_ptr<RegistryStreamHolder> StreamRegistryImpl::get_or_create_holder(const std::string& streamId) {
    std::scoped_lock<std::mutex> lk(mtx_);
    auto it = holders_.find(streamId);
    if (it != holders_.end()) return it->second;
    // Create a minimal holder if desired (requires appropriate defaults)
    // For this prototype, return nullptr to signal not found.
    return nullptr;
}

std::unique_ptr<StreamRegistry> MakeStreamRegistry(ServiceBus& bus) {
    return std::make_unique<StreamRegistryImpl>(bus);
}