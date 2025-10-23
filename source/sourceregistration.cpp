#include "source-factory.h"
#include "source-random.h"

// Register built-in/compile-time services
REGISTER_SOURCE_TYPE("Random", [](const std::string& name, const Schema& schema, StorageManager& storage) -> std::shared_ptr<ISource> {
    return RandomDataSource::Create(name, schema, storage);
});