#pragma once

#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <optional>

#include "service-bus.h"
#include "service-logger.h"
#include "service-source-registry.h"
#include "service-storage.h"
#include "source-factory.h"
#include "source.h"
#include "schema.h"
#include "project.h"
#include "hash.h"

namespace signal_stream {

    class ProjectManager {
    public:
        using SourcePtr = std::shared_ptr<ISource>;

        explicit ProjectManager(ServiceBus& bus,
            const std::string& path,
            boost::asio::io_context& ioc);

        ~ProjectManager();

        // Project lifecycle
        bool load_project(const ProjectData& pdata, bool autoStart, std::string& outError);
        bool load_project_from_file(const std::string& path, bool autoStart, std::string& outError);

        // Registry access
        const SourceRegistry& get_registry() const;
        bool is_source_registered(const std::string& name) const;
        std::vector<std::string> get_all_source_names() const;
        size_t get_source_count() const;

        // Source access
        SourcePtr get_source(const std::string& name) const;
        std::optional<Schema> get_source_schema(const std::string& name) const;
        std::unordered_map<std::string, SourcePtr> get_all_sources() const;

        // Source management
        bool add_source(const SourceData& desc, std::string& outError);
        bool remove_source(const std::string& name, std::string& outError);
        bool rename_source(const std::string& oldName, const std::string& newName, std::string& outError);

        // Service lifecycle
        bool start_service(const std::string& name, std::string& outError);
        bool stop_service(const std::string& name, std::string& outError);
        bool start_all_services(std::string& outError);
        void stop_all_services();

        // Storage access
        StreamBufferHandle get_buffer_handle(const std::string& sourceName, const StorageManager::StreamType type);
        float get_buffer_health(const std::string& sourceName) const;
        const StorageManager& get_storage() const;

        // Project metadata
        ProjectData get_project_data() const;
        std::string get_name() const;
        std::string get_path() const;
        uint32_t get_hash() const;

    private:
        // Internal helpers
        bool start_all_services_locked(std::string& outError);
        void stop_all_services_locked();
        void finalize_all_schemas();

        // Source management helpers
        bool add_source_locked(const SourceData& source, std::string& outError);

        // Members
        ServiceBus& bus_;
        boost::asio::io_context& ioc_;

        mutable std::mutex mtx_;
        ProjectData data_;

        std::string path_;
        uint32_t hash_;

        SourceRegistry registry_;
        StorageManager storage_;
        std::unordered_map<std::string, SourcePtr> sources_;
    };

    // Utility functions
    bool load_project_data_from_file(const std::string& path, ProjectData& outProject, std::string& outError);
    SourceData parse_source_data(const json& j);
    ProjectData parse_project_data(const json& j);

} // namespace signal_stream