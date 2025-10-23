#include <algorithm>
#include <cassert>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <memory>

#include "stream-registry.h"
#include "stream-registry-impl.h"

// Factory definition — hides concrete type
std::unique_ptr<StreamRegistry> make_stream_registry() {
    return std::make_unique<StreamRegistryImpl>();
}