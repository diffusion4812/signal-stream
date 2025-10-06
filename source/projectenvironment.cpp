#pragma once

#include "projectenvironment.h"

// Deserialize Stream from json (throws std::runtime_error on invalid data)
inline SourceData parseStream(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Stream entry is not an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Stream missing 'name' string");
    if (!j.contains("type") || !j["type"].is_string()) throw std::runtime_error("Stream missing 'type' string");
    if (!j.contains("signals") || !j["signals"].is_array()) throw std::runtime_error("Stream missing 'signals' array");

    SourceData stream;
    stream.name = j.at("name").get<std::string>();
    stream.type = j.at("type").get<std::string>();

    for (const auto& sig : j.at("signals")) {
        if (!sig.is_object()) throw std::runtime_error("Signal entry is not an object");
        if (!sig.contains("name") || !sig["name"].is_string()) throw std::runtime_error("Signal missing 'name' string");
        if (!sig.contains("type") || !sig["type"].is_string()) throw std::runtime_error("Signal missing 'type' string");
        if (!sig.contains("unit") || !sig["unit"].is_string()) throw std::runtime_error("Signal missing 'unit' string");

        stream.schema.add_field(sig.at("name").get<std::string>(), kindFromString(sig.at("type").get<std::string>()));
    }
    return stream;
}

// Deserialize Project from json (throws std::runtime_error on invalid data)
inline ProjectData parseProject(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Root JSON must be an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Project missing 'name' string");
    if (!j.contains("streams") || !j["streams"].is_array()) throw std::runtime_error("Project missing 'streams' array");

    ProjectData p;
    p.name = j.at("name").get<std::string>();

    for (const auto& item : j.at("streams")) {
        p.sources.push_back(parseStream(item));
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
