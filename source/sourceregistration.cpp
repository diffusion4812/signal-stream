#include "source-factory.h"
#include "source-random.h"
#include "source-mqtt.h"

// Register built-in/compile-time services
REGISTER_SOURCE_TYPE("Random", [](const std::string& name, const Schema& schema, StorageManager& storage, boost::asio::io_context& ioc) -> std::shared_ptr<ISource> {
    return RandomSource::Create(name, schema, storage, ioc);
});

REGISTER_SOURCE_TYPE("MQTT", [](const std::string& name, const Schema& schema, StorageManager& storage, boost::asio::io_context& ioc) -> std::shared_ptr<ISource> {
    return MQTTSource::Create(name, schema, storage, ioc);
});