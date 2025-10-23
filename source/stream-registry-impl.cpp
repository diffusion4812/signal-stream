#include <algorithm>
#include <cassert>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <memory>

#include "stream-registry-impl.h"

StreamRegistryImpl::StreamRegistryImpl() = default;
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

    // Lock released here before publishing
    publish_event(RegistryEventType::Created, streamId, meta);
    return true;
}

// Delete stream
bool StreamRegistryImpl::delete_stream(const std::string& streamId) {
    std::scoped_lock<std::mutex> lk(mtx_);
    auto it = holders_.find(streamId);
    if (it == holders_.end()) return false;

    // Cleanup runtime state if needed (buffers, files, etc.)
    holders_.erase(it);
    publish_event(RegistryEventType::Deleted, streamId, {});
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
    std::scoped_lock<std::mutex> lk(mtx_);
    auto it = holders_.find(streamId);
    if (it == holders_.end()) return false;

    // Apply updates to StreamHolder (e.g., opts, recordSizeBytes, etc.)
    // For example:
    // it->second->opts = updatedMeta.someOption;
    publish_event(RegistryEventType::Updated, streamId, updatedMeta);
    return true;
}

// Rename stream
bool StreamRegistryImpl::rename_stream(const std::string& oldName, const std::string& newName) {
    std::scoped_lock<std::mutex> lk(mtx_);
    auto it = holders_.find(oldName);
    if (it == holders_.end()) return false;

    auto holder = it->second;
    holders_.erase(it);
    holders_.emplace(newName, holder);
    publish_event(RegistryEventType::Renamed, newName, {});
    return true;
}

// Listener management
int StreamRegistryImpl::register_listener(Listener listener) {
    std::scoped_lock<std::mutex> lk(mtx_);
    int token = nextListenerToken_++;
    listeners_.emplace(token, std::move(listener));
    return token;
}

void StreamRegistryImpl::unregister_listener(int token) {
    std::scoped_lock<std::mutex> lk(mtx_);
    listeners_.erase(token);
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

void StreamRegistryImpl::lock_registry() {
    mtx_.lock();
}

void StreamRegistryImpl::unlock_registry() {
    mtx_.unlock();
}

std::shared_ptr<void> StreamRegistryImpl::subscribe_with_token(Listener listener) {
    int tok = register_listener(std::move(listener));
    // Simple RAII token: unregister on destruction
    return std::shared_ptr<void>(nullptr, [this, tok](void*) { unregister_listener(tok); });
}

void StreamRegistryImpl::publish_event(RegistryEventType type,
                                       const std::string& streamId,
    const StreamMetadata& meta) {
    // Copy tokens to avoid holding lock while invoking user callbacks
    std::vector<Listener> toNotify;
    {
        std::scoped_lock lk(mtx_);
        for (auto& kv : listeners_) {
            toNotify.push_back(kv.second);
        }
    } // lock released here

    // Call listeners without holding the lock
    for (auto& cb : toNotify) {
        if (cb) cb(type, streamId, meta);
    }
}