#pragma once

#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <optional>
#include <functional>

#include "stream-registry.h"
#include "service-bus.h"

class SourceRegistryImpl : public SourceRegistry {
public:
    SourceRegistryImpl(ServiceBus& bus);
    ~SourceRegistryImpl() override;

    // Lifecycle
    bool create_stream(const std::string& streamId, const StreamMetadata& meta) override;
    bool delete_stream(const std::string& streamId) override;
    bool update_stream(const std::string& streamId, const StreamMetadata& updatedMeta) override;
    bool rename_stream(const std::string& oldName, const std::string& newName) override;

    // Queries
    std::optional<std::shared_ptr<RegistryStreamHolder>> get_stream(const std::string& streamId) override;
    std::optional<StreamMetadata> get_stream_metadata(const std::string& streamId) const override;
    std::vector<std::string> list_stream_ids() const override;
    std::vector<StreamMetadata> list_stream_metadata() const override;

    void reconcile_state() override;
    void notify_of_external_change(const std::string& streamId) override;

    size_t stream_count() const override;

    std::shared_ptr<RegistryStreamHolder> get_or_create_holder(const std::string& streamId) override;

private:
    ServiceBus& bus_;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<RegistryStreamHolder>> holders_;
};

std::unique_ptr<SourceRegistry> MakeSourceRegistry(ServiceBus& bus);