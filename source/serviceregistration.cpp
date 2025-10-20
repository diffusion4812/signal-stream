#include "servicefactory.h"
#include "service-random.h"

// Register built-in/compile-time services
REGISTER_SERVICE_TYPE("Random", [](const std::string& name, const Schema& schema, StorageManager& storage) -> std::shared_ptr<IService> {
    return RandomDataService::Create(name, schema, storage);
});