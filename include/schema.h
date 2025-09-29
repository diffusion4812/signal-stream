#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <optional>
#include <iostream>
#include <algorithm>
#include <fstream>

enum class Kind { Int32, Int64, Float, Double, String, Blob };

// Field descriptor
struct FieldDesc {
    std::string name;
    Kind kind;
    std::size_t offset;
    std::size_t size;
    std::size_t align;
};

class Schema {
    std::vector<FieldDesc> fields_;
    std::size_t totalSize_ = 0;
    bool finalised_ = false;

    static std::size_t size_of(Kind k) {
        switch (k) {
        case Kind::Int32:  return sizeof(int32_t);
        case Kind::Int64:  return sizeof(int64_t);
        case Kind::Float:  return sizeof(float);
        case Kind::Double: return sizeof(double);
        case Kind::String: return sizeof(char*); // store pointer
        case Kind::Blob:   return sizeof(void*); // store pointer
        }
        return 1;
    }
    static std::size_t align_of(Kind k) {
        switch (k) {
        case Kind::Int32:  return alignof(int32_t);
        case Kind::Int64:  return alignof(int64_t);
        case Kind::Float:  return alignof(float);
        case Kind::Double: return alignof(double);
        case Kind::String: return alignof(char*);
        case Kind::Blob:   return alignof(void*);
        }
        return 1;
    }
    static std::size_t align_up(std::size_t off, std::size_t a) { return (off + a - 1) & ~(a - 1); }

public:
    void add_field(std::string name, Kind k) {
        if (finalised_) throw std::logic_error("Schema already finalised");
        fields_.push_back({ std::move(name), k, 0, size_of(k), align_of(k) });
    }

    void finalise() {
        if (finalised_) return;
        std::size_t off = 0;
        std::size_t maxAlign = 1;
        for (auto& f : fields_) {
            maxAlign = std::max(maxAlign, f.align);
            off = align_up(off, f.align);
            f.offset = off;
            off += f.size;
        }
        totalSize_ = align_up(off, maxAlign);
        finalised_ = true;
    }

    bool isfinalised() {
        return finalised_;
    }

    std::size_t instance_size() const {
        if (!finalised_) throw std::logic_error("Schema not finalised");
        return totalSize_;
    }

    const FieldDesc* find(const std::string& name) const {
        for (const auto& f : fields_) if (f.name == name) return &f;
        return nullptr;
    }

    const std::vector<FieldDesc>& fields() const { return fields_; }
};