#pragma once

#include "projectenvironment.h"

#include "source-random.h"
#include "source-mqtt.h"

// Deserialize Stream from json (throws std::runtime_error on invalid data)
inline SourceData parseSource(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Source entry is not an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Source missing 'name' string");
    if (!j.contains("type") || !j["type"].is_string()) throw std::runtime_error("Source missing 'type' string");
    if (!j.contains("signals") || !j["signals"].is_array()) throw std::runtime_error("Source missing 'signals' array");

    SourceData source;
    source.name = j.at("name").get<std::string>();
    source.type = j.at("type").get<std::string>();

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
inline ProjectData parseProject(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Root JSON must be an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Project missing 'name' string");
    if (!j.contains("sources") || !j["sources"].is_array()) throw std::runtime_error("Project missing 'sources' array");

    ProjectData p;
    p.name = j.at("name").get<std::string>();

    for (const auto& item : j.at("sources")) {
        p.sources.push_back(parseSource(item));
    }

    return p;
}

bool LoadProjectFromFile(const std::string& path, ProjectData& outProject, std::string& outError) {
    std::ifstream ifs(path);
    if (!ifs) {
        outError = "Failed to open file: " + path;
        return false;
    }

    try {
        json j;
        ifs >> j;
        outProject = parseProject(j);
        return true;
    }
    catch (const std::exception& ex) {
        outError = std::string("Error parsing project JSON: ") + ex.what();
        return false;
    }
}
