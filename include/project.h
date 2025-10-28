#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <json.hpp>

#include "schema.h"

using json = nlohmann::json;

struct IMetadata {
    virtual ~IMetadata() = default;
};

struct SourceData {
    std::string name;
    std::string type;
    std::shared_ptr<IMetadata> metadata;
    Schema schema;
};

struct ProjectData {
    std::string name;
    std::vector<SourceData> sources;
};

