#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <json.hpp>

#include "service-storage.h"
#include "schema.h"

namespace signal_stream {

    using json = nlohmann::json;

    struct IMetadata {
        virtual ~IMetadata() = default;
    };

    struct SourceData {
        std::string name = "";
        std::string type = "";
        StreamStorageOptions storage_options = StreamStorageOptions();
        std::shared_ptr<IMetadata> metadata;
        Schema schema = Schema();
    };

    struct ProjectData {
        std::string name = "";
        std::vector<SourceData> sources;
    };

} // namespace signal_stream
