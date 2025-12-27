#include "service-source-registry.h"

SourceRegistry::SourceRegistry(ServiceBus& bus)
    : bus_(bus) {
}

SourceRegistry::~SourceRegistry() = default;

bool SourceRegistry::register_source(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    {
        std::scoped_lock lock(mtx_);

        // Check if already exists
        if (source_names_.contains(name)) {
            return false;  // Already registered
        }

        source_names_.insert(name);
    }

    // Publish event (outside lock)
    bus_.Publish<Event>(Event{
        .type = Event::Type::Registered,
        .source_name = name,
        .old_name = std::nullopt
        });

    return true;
}

bool SourceRegistry::unregister_source(const std::string& name) {
    {
        std::scoped_lock lock(mtx_);

        // Try to erase
        if (source_names_.erase(name) == 0) {
            return false;  // Not found
        }
    }

    // Publish event
    bus_.Publish<Event>(Event{
        .type = Event::Type::Unregistered,
        .source_name = name,
        .old_name = std::nullopt
        });

    return true;
}

bool SourceRegistry::rename_source(const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) {
        return false;
    }

    {
        std::scoped_lock lock(mtx_);

        // Check old name exists
        if (!source_names_.contains(oldName)) {
            return false;
        }

        // Check new name doesn't exist
        if (source_names_.contains(newName)) {
            return false;
        }

        // Perform rename
        source_names_.erase(oldName);
        source_names_.insert(newName);
    }

    // Publish event
    bus_.Publish<Event>(Event{
        .type = Event::Type::Renamed,
        .source_name = newName,
        .old_name = oldName
        });

    return true;
}

bool SourceRegistry::is_registered(const std::string& name) const {
    std::scoped_lock lock(mtx_);
    return source_names_.contains(name);
}

std::vector<std::string> SourceRegistry::list_all_sources() const {
    std::scoped_lock lock(mtx_);
    return std::vector<std::string>(source_names_.begin(), source_names_.end());
}

size_t SourceRegistry::count() const {
    std::scoped_lock lock(mtx_);
    return source_names_.size();
}

void SourceRegistry::clear() {
    std::scoped_lock lock(mtx_);
    source_names_.clear();
}