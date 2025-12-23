#pragma once

#include "service-storage.h"

// Null backend (no-op)
struct NullBackend : public IStorageBackend {
    bool write_batch_two_pass(const std::string& streamId, const StreamBuffer::BatchChunks& chunks) {
        return true;
    }
};