#include <variant>

#include "service-storage-backend-null.h"
#include "service-storage-backend-csv.h"
#include "service-storage-backend-parquet.h"

namespace signal_stream {

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
            const Schema& schema);
    private:
        // Compile-time check for exhaustive variant handling
        template<typename> struct always_false : std::false_type {};
    };

} // namespace signal_stream