#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <json.hpp>

using json = nlohmann::json;

struct Signal {
    std::string name;
    std::string type;
    std::string unit;
};

struct Stream {
    std::string name;
    std::string type;
    std::vector<Signal> signals;
};

struct Project {
    std::string name;
    std::vector<Stream> streams;
};

// Deserialize Stream from json (throws std::runtime_error on invalid data)
inline Stream parseStream(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Stream entry is not an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Stream missing 'name' string");
    if (!j.contains("type") || !j["type"].is_string()) throw std::runtime_error("Stream missing 'type' string");
    if (!j.contains("signals") || !j["signals"].is_array()) throw std::runtime_error("Stream missing 'signals' array");

    Stream stream;
    stream.name = j.at("name").get<std::string>();
    stream.type = j.at("type").get<std::string>();

    for (const auto& sig : j.at("signals")) {
        if (!sig.is_object()) throw std::runtime_error("Signal is not an object");
        if (!sig.contains("name") || !sig["name"].is_string()) throw std::runtime_error("Signal missing 'name' string");
        if (!sig.contains("type") || !sig["type"].is_string()) throw std::runtime_error("Signal missing 'type' string");
        if (!sig.contains("unit") || !sig["unit"].is_string()) throw std::runtime_error("Signal missing 'unit' string");
        Signal s;
        s.name = sig.at("name").get<std::string>();
        s.type = sig.at("type").get<std::string>();
        s.unit = sig.at("unit").get<std::string>();
        stream.signals.push_back(s);
    }
    return stream;
}

// Deserialize Project from json (throws std::runtime_error on invalid data)
inline Project parseProject(const json& j) {
    if (!j.is_object()) throw std::runtime_error("Root JSON must be an object");
    if (!j.contains("name") || !j["name"].is_string()) throw std::runtime_error("Project missing 'name' string");
    if (!j.contains("streams") || !j["streams"].is_array()) throw std::runtime_error("Project missing 'streams' array");

    Project p;
    p.name = j.at("name").get<std::string>();

    for (const auto& item : j.at("streams")) {
        p.streams.push_back(parseStream(item));
    }

    return p;
}

bool LoadProjectFromFile(const std::string& path, Project& outProject, std::string& outError) {
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