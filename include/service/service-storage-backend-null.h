#pragma once

#include "service-storage.h"

// Null backend (no-op)
struct NullBackend : public StorageBackend {
    bool write_batch(const std::string& streamId, const std::vector<std::byte>& batch) {
        return true;
    }
};