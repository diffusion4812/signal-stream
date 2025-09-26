#include "servicefactory.h"
#include "service-random.h"

// Register built-in/compile-time services
REGISTER_SERVICE_TYPE("Random", [](const SourceData& d) -> std::shared_ptr<IService> {
    return RandomDataService::Create(d, 8);
    });