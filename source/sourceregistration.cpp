#include "source-factory.h"
#include "source-random.h"
#include "source-mqtt.h"

// Register built-in/compile-time services
REGISTER_SOURCE_TYPE_WITH_META("Random", RandomSource);
REGISTER_SOURCE_TYPE_WITH_META("MQTT", MQTTSource);