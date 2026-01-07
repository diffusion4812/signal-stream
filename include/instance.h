#pragma once
#include "schema.h"
#include <vector>
#include <optional>
#include <cstring>
#include <stdexcept>

namespace signal_stream {

    class Record {
    public:
        explicit Record(Schema s)
            : schema_(s), data_(s.instance_size(), std::byte{ 0 }) {
        }

        // Setters for arithmetic types
        template<typename T>
        void set(const std::string& name, T value) {
            const FieldDesc* f = schema_.find(name);
            if (!f) throw std::out_of_range("field not found: " + name);

            if constexpr (std::is_same_v<T, int32_t>) {
                if (f->kind != Kind::Int32) throw std::bad_cast();
                *reinterpret_cast<int32_t*>(ptr_at(f->offset)) = value;
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                if (f->kind != Kind::Int64) throw std::bad_cast();
                *reinterpret_cast<int64_t*>(ptr_at(f->offset)) = value;
            }
            else if constexpr (std::is_same_v<T, float>) {
                if (f->kind != Kind::Float) throw std::bad_cast();
                *reinterpret_cast<float*>(ptr_at(f->offset)) = value;
            }
            else if constexpr (std::is_same_v<T, double>) {
                if (f->kind != Kind::Double) throw std::bad_cast();
                *reinterpret_cast<double*>(ptr_at(f->offset)) = value;
            }
            else {
                static_assert(!std::is_same_v<T, T>, "Unsupported type for this overload; use set_string/set_blob");
            }
        }

        // String setter (copies content)
        void set_string(const std::string& name, const std::string& s) {
            const FieldDesc* f = schema_.find(name);
            if (!f) throw std::out_of_range("field not found: " + name);
            if (f->kind != Kind::String) throw std::bad_cast();

            char** slot = reinterpret_cast<char**>(ptr_at(f->offset));
            delete[] * slot;
            char* copy = new char[s.size() + 1];
            std::memcpy(copy, s.c_str(), s.size() + 1);
            *slot = copy;
        }

        // Blob setter (store heap-allocated vector)
        void set_blob(const std::string& name, const std::vector<std::byte>& blob) {
            const FieldDesc* f = schema_.find(name);
            if (!f) throw std::out_of_range("field not found: " + name);
            if (f->kind != Kind::Blob) throw std::bad_cast();

            void** slot = reinterpret_cast<void**>(ptr_at(f->offset));
            auto newVec = new std::vector<std::byte>(blob);
            auto oldVec = reinterpret_cast<std::vector<std::byte>*>(*slot);
            delete oldVec;
            *slot = newVec;
        }

        // Getters
        template<typename T>
        std::optional<T> get(const std::string& name) const {
            const FieldDesc* f = schema_.find(name);
            if (!f) return std::nullopt;

            if constexpr (std::is_same_v<T, int32_t>) {
                if (f->kind != Kind::Int32) return std::nullopt;
                return *reinterpret_cast<const int32_t*>(ptr_at(f->offset));
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                if (f->kind != Kind::Int64) return std::nullopt;
                return *reinterpret_cast<const int64_t*>(ptr_at(f->offset));
            }
            else if constexpr (std::is_same_v<T, float>) {
                if (f->kind != Kind::Float) return std::nullopt;
                return *reinterpret_cast<const float*>(ptr_at(f->offset));
            }
            else if constexpr (std::is_same_v<T, double>) {
                if (f->kind != Kind::Double) return std::nullopt;
                return *reinterpret_cast<const double*>(ptr_at(f->offset));
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                if (f->kind != Kind::String) return std::nullopt;
                char* const* slot = reinterpret_cast<char* const*>(ptr_at(f->offset));
                if (!*slot) return std::string{};
                return std::string(*slot);
            }
            else if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
                if (f->kind != Kind::Blob) return std::nullopt;
                void* const* slot = reinterpret_cast<void* const*>(ptr_at(f->offset));
                auto vecPtr = reinterpret_cast<std::vector<std::byte>*>(*slot);
                if (!vecPtr) return std::vector<std::byte>{};
                return *vecPtr;
            }
            else {
                return std::nullopt;
            }
        }

        std::string get_as_string(const std::string& name) {
            const FieldDesc* f = schema_.find(name);
            if (!f) return "";

            if (f->kind == Kind::Int32) return std::to_string(*reinterpret_cast<const int32_t*>(ptr_at(f->offset)));
            else if (f->kind == Kind::Int64) return std::to_string(*reinterpret_cast<const int64_t*>(ptr_at(f->offset)));
            else if (f->kind == Kind::Float) return std::to_string(*reinterpret_cast<const float*>(ptr_at(f->offset)));
            else if (f->kind == Kind::Double) return std::to_string(*reinterpret_cast<const double*>(ptr_at(f->offset)));
            else if (f->kind == Kind::String) {
                char* const* slot = reinterpret_cast<char* const*>(ptr_at(f->offset));
                if (!*slot) return std::string{};
                return std::string(*slot);
            }
            else {
                return "";
            }
        }

        std::byte* get_data() noexcept {
            return data_.data();
        }

        void set_data(const void* src) {
            std::memcpy(data_.data(), src, schema_.instance_size());
        }

    private:
        Schema schema_;
        std::vector<std::byte> data_; // safer than unique_ptr

        void* ptr_at(std::size_t offset) noexcept { return static_cast<void*>(data_.data() + offset); }
        const void* ptr_at(std::size_t offset) const noexcept { return static_cast<const void*>(data_.data() + offset); }
    };

}