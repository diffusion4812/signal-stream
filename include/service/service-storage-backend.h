#pragma once

#include "variant"

#include "service-storage.h"

// Abstract persistence backend
struct IStorageBackend {
    virtual ~IStorageBackend() = default;
    virtual bool write(const std::string& streamId, const StreamBuffer::BatchChunks& chunks) = 0;
    virtual void flush() = 0;
    virtual void close() = 0;

    // Optional: backends can provide metrics
    virtual size_t get_total_records() const { return 0; }
    virtual size_t get_total_bytes() const { return 0; }
};

#include "service-storage-backend-null.h"
#include "service-storage-backend-csv.h"
#include "service-storage-backend-parquet.h"

// Null backend (for testing)
struct NullBackendConfig {
    // No file rotation needed
};

using BackendConfig = std::variant<
    NullBackendConfig,
    CSVBackend::Config,
    ParquetBackend::Config
>;

class BackendFactory {
public:
    static std::unique_ptr<IStorageBackend> create(
        const BackendConfig& config,
        const Schema& schema) {

        return std::visit([&schema](auto&& cfg) -> std::unique_ptr<IStorageBackend> {
            using T = std::decay_t<decltype(cfg)>;

            if constexpr (std::is_same_v<T, NullBackendConfig>) {
                return std::make_unique<NullBackend>();
            }
            else if constexpr (std::is_same_v<T, CSVBackend::Config>) {
                CSVBackend::Config backend_config;
                backend_config.rotation = cfg.rotation;
                backend_config.delimiter = cfg.delimiter;
                backend_config.include_header = cfg.include_header;
                backend_config.precision = cfg.precision;

                return std::make_unique<CSVBackend>(schema, backend_config);
            }
            else if constexpr (std::is_same_v<T, ParquetBackend::Config>) {
                ParquetBackend::Config backend_config;
                backend_config.rotation = cfg.rotation;

                return std::make_unique<ParquetBackend>(schema, backend_config);
            }
            else {
                static_assert(always_false<T>::value, "Unhandled backend config type");
            }
        }, config);
    }

private:
    // Compile-time check for exhaustive variant handling
    template<typename> struct always_false : std::false_type {};
};