#pragma once

#include "service-storage-backend.h"

// Null backend (no-op)
struct NullBackend : public IStorageBackend {
    bool write(const std::string& streamId, const StreamBuffer::BatchChunks& chunks) {
        return true;
    }

    void flush() {}
    void close() {}
};