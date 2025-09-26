#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <json.hpp>

using json = nlohmann::json;

struct SignalData {
    std::string name;
    std::string type;
    std::string unit;
};

struct SourceData {
    std::string name;
    std::string type;
    std::vector<SignalData> signals;

};

struct ProjectData {
    std::string name;
    std::vector<SourceData> streams;
};

