#pragma once

#include "service-project.h"

#include "source-mqtt.h"

// Deserialize Stream from json (throws std::runtime_error on invalid data)
inline SourceData parse_source_data(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Source entry is not an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Source missing 'name' string");
    if (!j.contains("type") || !j["type"].is_string()) throw std::runtime_error("Source missing 'type' string");
    if (!j.contains("storage") || !j["storage"].is_object()) throw std::runtime_error("Source missing 'storage' object");
    if (!j.contains("signals") || !j["signals"].is_array()) throw std::runtime_error("Source missing 'signals' array");

    SourceData source;
    source.name = j.at("name").get<std::string>();
    source.type = j.at("type").get<std::string>();
    json k = j.at("storage");
    if (!k.contains("capacity") || !k["capacity"].is_number_integer()) throw std::runtime_error("Storage missing 'capacity' integer");
    if (!k.contains("flush_size") || !k["flush_size"].is_number_integer()) throw std::runtime_error("Storage missing 'flush_size' integer");
    if (!k.contains("flush_interval") || !k["flush_interval"].is_number_integer()) throw std::runtime_error("Storage missing 'flush_interval' integer");
    source.storage_options.capacity_records = k.at("capacity").get<size_t>();
    source.storage_options.flush_batch_size = k.at("flush_size").get<size_t>();
    size_t millis = k.at("flush_interval").get<size_t>();
    source.storage_options.flush_interval = std::chrono::milliseconds(millis);
    source.storage_options.backend_config = ParquetBackend::Config{
        FileRotationConfig{
            100'000,                   // max_records_per_file
            100 * 1024 * 1024,         // max_file_size_bytes
            std::chrono::milliseconds(5'000), // max_file_duration
            "./data",                  // output_directory
            "{stream}_{timestamp}_{sequence}", // filename_pattern
            nullptr,                   // on_file_created
            nullptr                    // on_file_closed
        }
	};

    if (source.type == "MQTT") {
        if (!j.contains("metadata") || !j["metadata"].is_object()) throw std::runtime_error("Source missing 'metadata' object");
        json k = j.at("metadata");
        if (!k.contains("topic") || !k["topic"].is_string()) throw std::runtime_error("Metadata missing 'topic' string");
        auto metadata = std::make_unique<MQTTSource::Metadata>();
        metadata->topic = k.at("topic").get<std::string>();
        source.metadata = std::move(metadata);
    }

    for (const auto& sig : j.at("signals")) {
        if (!sig.is_object()) throw std::runtime_error("Signal entry is not an object");
        if (!sig.contains("name") || !sig["name"].is_string()) throw std::runtime_error("Signal missing 'name' string");
        if (!sig.contains("type") || !sig["type"].is_string()) throw std::runtime_error("Signal missing 'type' string");
        if (!sig.contains("unit") || !sig["unit"].is_string()) throw std::runtime_error("Signal missing 'unit' string");

        source.schema.add_field(sig.at("name").get<std::string>(), kindFromString(sig.at("type").get<std::string>()));
    }
    return source;
}

// Deserialize Project from json (throws std::runtime_error on invalid data)
inline ProjectData parse_project_data(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Root JSON must be an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Project missing 'name' string");
    if (!j.contains("sources") || !j["sources"].is_array()) throw std::runtime_error("Project missing 'sources' array");

    ProjectData p;
    p.name = j.at("name").get<std::string>();

    for (const auto& item : j.at("sources")) {
        p.sources.push_back(parse_source_data(item));
    }

    return p;
}

bool load_project_from_file(const std::string& path, ProjectData& out_project, std::string& out_error) {
    std::ifstream ifs(path);
    if (!ifs) {
        out_error = "Failed to open file: " + path;
        return false;
    }

    try {
        json j;
        ifs >> j;
        out_project = parse_project_data(j);
        return true;
    }
    catch (const std::exception& ex) {
        out_error = std::string("Error parsing project JSON: ") + ex.what();
        return false;
    }
}

bool load_project_data_from_file(const std::string& path, ProjectData& out_project, std::string& out_error) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            out_error = "Failed to open file: " + path;
            return false;
        }

        json j;
        file >> j;

        out_project = parse_project_data(j);
        return true;
    }
    catch (const std::exception& ex) {
        out_error = "Failed to parse project file: " + std::string(ex.what());
        return false;
    }
}

// ============================================================================
// ProjectManager Implementation
// ============================================================================

ProjectManager::ProjectManager(ServiceBus& bus,
    const std::string& path,
    boost::asio::io_context& ioc)
    : bus_(bus)
    , path_(path)
    , ioc_(ioc)
    , hash_(fnv1a_32(path))
    , registry_(bus)
    , storage_()
{
    std::string err;
	load_project_from_file(path_, false, err);
}

ProjectManager::~ProjectManager() {
    stop_all_services();
}

bool ProjectManager::load_project(const ProjectData& pdata, bool autoStart, std::string& outError) {
    TRACE_FUNCTION_SCOPE(bus_);
    std::scoped_lock lock(mtx_);

    // Clear existing
    stop_all_services_locked();
    registry_.clear();

    data_ = pdata;
    finalize_all_schemas();

    // Create sources with coordinated setup
    for (const SourceData& source_data : data_.sources) {
        if (sources_.contains(source_data.name)) {
            outError = "Duplicate source name: " + source_data.name;
            sources_.clear();
            return false;
        }

        // Step 1: Register in registry
        if (!registry_.register_source(source_data.name)) {
            outError = "Failed to register source: " + source_data.name;
            sources_.clear();
            return false;
        }

        // Step 2: Create buffer
        if (!storage_.create_stream(source_data.name, source_data.storage_options, source_data.schema)) {
            outError = "Failed to create buffer for source: " + source_data.name;
            registry_.unregister_source(source_data.name);
            sources_.clear();
            return false;
        }

        // Step 3: Create source instance
        auto source = create_source_by_type(
            bus_,
            source_data.name,
            source_data.type,
            *source_data.metadata,
            source_data.schema,
            storage_,
            ioc_
        );

        if (!source) {
            outError = "Unknown source type: " + source_data.type + " (source: " + source_data.name + ")";
            storage_.flush_stream(source_data.name);
            registry_.unregister_source(source_data.name);
            sources_.clear();
            return false;
        }

        sources_.emplace(source_data.name, std::move(source));
    }

    if (autoStart) {
        return start_all_services_locked(outError);
    }

    return true;
}

bool ProjectManager::load_project_from_file(const std::string& path, bool autoStart, std::string& outError) {
    ProjectData pdata;
    if (!load_project_data_from_file(path, pdata, outError)) {
        return false;
    }

    path_ = path;
    hash_ = fnv1a_32(path);

    return load_project(pdata, autoStart, outError);
}

// ============================================================================
// Registry Access
// ============================================================================

const SourceRegistry& ProjectManager::get_registry() const {
    return registry_;
}

bool ProjectManager::is_source_registered(const std::string& name) const {
    return registry_.is_registered(name);
}

std::vector<std::string> ProjectManager::get_all_source_names() const {
    return registry_.list_all_sources();
}

size_t ProjectManager::get_source_count() const {
    return registry_.count();
}

// ============================================================================
// Source Access
// ============================================================================

ProjectManager::SourcePtr ProjectManager::get_source(const std::string& name) const {
    std::scoped_lock lock(mtx_);
    auto it = sources_.find(name);
    return (it != sources_.end()) ? it->second : nullptr;
}

std::optional<Schema> ProjectManager::get_source_schema(const std::string& name) const {
    auto source = get_source(name);
    if (!source) return std::nullopt;
    return source->GetSchema();
}

std::unordered_map<std::string, ProjectManager::SourcePtr> ProjectManager::get_all_sources() const {
    std::scoped_lock lock(mtx_);
    return sources_;  // Returns copy of map
}

// ============================================================================
// Source Management
// ============================================================================

bool ProjectManager::add_source(const SourceData& source, std::string& outError) {
    std::scoped_lock lock(mtx_);

    // Check for duplicate
    if (sources_.contains(source.name)) {
        outError = "Source already exists: " + source.name;
        return false;
    }

    // Step 1: Register in registry
    if (!registry_.register_source(source.name)) {
        outError = "Failed to register source: " + source.name;
        return false;
    }

    // Step 2: Create buffer
	StreamStorageOptions opts;
    if (!storage_.create_stream(source.name, opts, source.schema)) {
        outError = "Failed to create buffer for source: " + source.name;
        registry_.unregister_source(source.name);
        return false;
    }

    // Step 3: Create source instance
    auto source_ptr = create_source_by_type(
        bus_,
        source.name,
        source.type,
        *source.metadata,
        source.schema,
        storage_,
        ioc_
    );

    if (!source_ptr) {
        outError = "Unknown source type: " + source.type;
        storage_.flush_stream(source.name);
        registry_.unregister_source(source.name);
        return false;
    }

    // Finalize schema if needed
    if (!source_ptr->GetSchema().is_finalized()) {
        source_ptr->GetSchema().finalize();
    }

    sources_.emplace(source.name, std::move(source_ptr));

    // Add to project data
    data_.sources.push_back(source);

    return true;
}

bool ProjectManager::remove_source(const std::string& name, std::string& outError) {
    /*std::scoped_lock lock(mtx_);

    auto it = sources_.find(name);
    if (it == sources_.end()) {
        outError = "Source not found: " + name;
        return false;
    }

    // Step 1: Stop the source if running
    try {
        it->second->Stop();
    }
    catch (const std::exception& ex) {
        outError = "Failed to stop source: " + std::string(ex.what());
        return false;
    }

    // Step 2: Remove from sources map
    sources_.erase(it);

    // Step 3: Cleanup buffer
    storage_.CleanupSource(name);

    // Step 4: Unregister from registry
    registry_.UnregisterSource(name);

    // Step 5: Remove from project data
    auto& sourcesVec = data_.sources;
    sourcesVec.erase(
        std::remove_if(sourcesVec.begin(), sourcesVec.end(),
            [&name](const SourceData& s) { return s.name == name; }),
        sourcesVec.end()
    );*/

    return true;
}

bool ProjectManager::rename_source(const std::string& oldName, const std::string& newName, std::string& outError) {
    /*std::scoped_lock lock(mtx_);

    // Validate names
    if (oldName.empty() || newName.empty()) {
        outError = "Invalid source name";
        return false;
    }

    if (oldName == newName) {
        return true;  // No-op
    }

    // Check old name exists
    auto it = sources_.find(oldName);
    if (it == sources_.end()) {
        outError = "Source not found: " + oldName;
        return false;
    }

    // Check new name doesn't exist
    if (sources_.contains(newName)) {
        outError = "Target name already exists: " + newName;
        return false;
    }

    // Step 1: Rename in registry
    if (!registry_.RenameSource(oldName, newName)) {
        outError = "Failed to rename in registry";
        return false;
    }

    // Step 2: Rename in storage
    if (!storage_.RenameSource(oldName, newName)) {
        outError = "Failed to rename in storage";
        registry_.RenameSource(newName, oldName);  // Rollback
        return false;
    }

    // Step 3: Update source instance (if source tracks its own name)
    auto source = it->second;
    // Note: If ISource has a SetName method, call it here
    // source->SetName(newName);

    // Step 4: Move in sources map
    sources_.erase(it);
    sources_.emplace(newName, std::move(source));

    // Step 5: Update project data
    for (auto& sourceData : data_.sources) {
        if (sourceData.name == oldName) {
            sourceData.name = newName;
            break;
        }
    }*/

    return true;
}

// ============================================================================
// Service Lifecycle
// ============================================================================

bool ProjectManager::start_service(const std::string& name, std::string& outError) {
    std::scoped_lock lock(mtx_);

    auto it = sources_.find(name);
    if (it == sources_.end()) {
        outError = "Source not found: " + name;
        return false;
    }

    try {
        it->second->Start();
        return true;
    }
    catch (const std::exception& ex) {
        outError = "Failed to start source: " + std::string(ex.what());
        return false;
    }
}

bool ProjectManager::stop_service(const std::string& name, std::string& outError) {
    std::scoped_lock lock(mtx_);

    auto it = sources_.find(name);
    if (it == sources_.end()) {
        outError = "Source not found: " + name;
        return false;
    }

    try {
        it->second->Stop();
    }
    catch (const std::exception& ex) {
        outError = "Failed to stop source: " + std::string(ex.what());
        return false;
    }

	return true;
}

bool ProjectManager::start_all_services(std::string& outError) {
    std::scoped_lock lock(mtx_);
    return start_all_services_locked(outError);
}

void ProjectManager::stop_all_services() {
    std::scoped_lock lock(mtx_);
    stop_all_services_locked();
}

// ============================================================================
// Storage Access
// ============================================================================

StreamBufferHandle ProjectManager::get_buffer_handle(const std::string& sourceName) {
    auto handleOpt = storage_.GetBufferHandle(sourceName);
    if (!handleOpt) {
        throw std::runtime_error("Buffer not found for source: " + sourceName);
    }
    return std::move(*handleOpt);
}

float ProjectManager::get_buffer_health(const std::string& sourceName) const {
    return storage_.GetBufferHealth(sourceName).value_or(0.0f);
}

const StorageManager& ProjectManager::get_storage() const {
    return storage_;
}

// ============================================================================
// Project Metadata
// ============================================================================

ProjectData ProjectManager::get_project_data() const {
    std::scoped_lock lock(mtx_);
    return data_;
}

std::string ProjectManager::get_name() const {
    std::scoped_lock lock(mtx_);
    return data_.name;
}

std::string ProjectManager::get_path() const {
    return path_;
}

uint32_t ProjectManager::get_hash() const {
    return hash_;
}

// ============================================================================
// Private Helpers
// ============================================================================

bool ProjectManager::start_all_services_locked(std::string& outError) {
    for (auto& [name, source] : sources_) {
        try {
            if (source->Status() == SourceStatus::Stopped ||
                source->Status() == SourceStatus::Error) {
                source->Start();
            }
        }
        catch (const std::exception& ex) {
            outError = "Failed to start source '" + name + "': " + ex.what();
            return false;
        }
    }
    return true;
}

void ProjectManager::stop_all_services_locked() {
    for (auto& [name, source] : sources_) {
        try {
            if (source->Status() != SourceStatus::Stopped) {
                source->Stop();
            }
        }
        catch (const std::exception& ex) {
            // Early return on first failure
            return;
        }
    }
    return;
}

void ProjectManager::finalize_all_schemas() {
    for (auto& sourceData : data_.sources) {
        if (!sourceData.schema.is_finalized()) {
            sourceData.schema.finalize();
        }
    }
}