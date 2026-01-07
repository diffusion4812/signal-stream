#include "service-storage-backend-factory.h"

namespace signal_stream {

    std::unique_ptr<IStorageBackend> BackendFactory::create(
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

} // namespace signal_stream