#pragma once

#include <vector>
#include <queue>
#include <random>
#include <atomic>
#include <thread>
#include <mutex>

#include "source.h"

#include <Windows.h>
#include "ProcessAttachment.h"
#include "SymbolResolver.h"
#include "SymbolFilter.h"
#include "TypeCache.h"

namespace signal_stream {

    class ProcessSource : public Source {
    public:
        struct Metadata : IMetadata {

        };

        // Create with a descriptor and a buffer capacity
        static std::shared_ptr<ProcessSource> Create(ServiceBus& bus, const std::string& name, const Schema& schema, const Metadata& metadata, StorageManager& storage, boost::asio::io_context& ioc) {
            return std::make_shared<ProcessSource>(bus, name, schema, metadata, storage, ioc);
        }

        ProcessSource(ServiceBus& bus, const std::string& name, const Schema& schema, const Metadata& metadata, StorageManager& storage, boost::asio::io_context& ioc) :
            Source(bus, name, schema, storage, ioc),
            bus_(bus) {
            resolver_.load_pdb(L"C:\\temp\\hello.pdb", L"C:\\temp\\hello.exe");
            std::vector<Symbol> all_symbols = resolver_.find_all_symbols();

            filter_.initialise_from_pdb(resolver_.get_session(), resolver_.get_global_scope());

            std::vector<Symbol> filtered_symbols1;
            for (const Symbol& symbol : all_symbols) {
                if (filter_.is_in_user_address_space(symbol.virtual_address_) &&
                    filter_.is_user_compiland(symbol.module_name_)
                    ) {
                    filtered_symbols1.push_back(symbol);
                }
            }

            std::vector<Symbol> filtered_symbols2;
            filtered_symbols2 = filter_.filter_symbols(filtered_symbols1, std::string("^pv"));

            TypeCache cache(resolver_.get_session());
            cache.resolve_type_recursive(filtered_symbols2[0].type_id_);

            process_.attach(40284);
            addr_ = (void*)(process_.get_module_base_address(40284, L"C:\\temp\\hello.exe") + filtered_symbols2[0].rva_);
        }

        // Non-blocking attempt to acquire one sample buffer.
        bool DoTryAcquireSample(SampleHandle& outHandle, Record& instance) override
        {
            return true;
        }
        // Blocking acquire with timeout: in this simple generator case we ignore timeout
        // and behave identical to TryAcquireSample (always returns immediately).
        bool DoAcquireSample(std::chrono::milliseconds /*timeout*/,
            SampleHandle& outHandle,
            const std::byte*& outData,
            size_t& outSize,
            Sample& outMeta) override
        {
            // For an on-demand random generator there's no waiting: just produce synchronously.
            return false; // TryAcquireSample(outHandle, outData, outSize, outMeta);
        }

        // Release previously acquired buffer identified by handle.
        void DoReleaseSample(SampleHandle handle) override
        {
        }

    protected:
        bool DoOnStart() override {
            return true;
        }

        void RunOnce() override {
            Record instance(schema_.value());

            int val = 0;
            process_.read_memory((LPCVOID)addr_, sizeof(int), &val);

            for (const auto& f : schema_.value().fields()) {
                if (f.name == "_timestamp") {
                    int64_t timestamp = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                    instance.set<int64_t>("_timestamp", timestamp);
                    continue;
                }
                switch (f.kind) {
                case Kind::Int32:
                    instance.set<int32_t>(f.name, static_cast<int32_t>(val));
                    break;
                }
            }

            const auto* begin = reinterpret_cast<const std::byte*>(instance.get_data());
            const auto* end = begin + schema_->instance_size();
            SubmitResult r = token_.try_submit(
                std::vector<std::byte>(begin, end)
            );

            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(20), [this] { return !running_.load(); });
        }

    private:
        ServiceBus& bus_;
        SymbolResolver resolver_;
        ProcessAttachment process_;
        SymbolFilter filter_;
        std::unique_ptr<TypeCache> cache_;

        void* addr_;
    };

} // namespace signal_stream