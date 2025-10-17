#include "schema.h"

Kind kindFromString(std::string type_str) {
    switch (fnv1a_32(type_str)) {
        case fnv1a_32("int32"):  return Kind::Int32;
        case fnv1a_32("int64"):  return Kind::Int64;
        case fnv1a_32("float"):  return Kind::Float;
        case fnv1a_32("double"):  return Kind::Double;
        case fnv1a_32("string"):  return Kind::String;
        case fnv1a_32("blob"):  return Kind::Blob;
        default: throw std::runtime_error("Unsupported type: " + type_str);
    }
    return Kind::Int32; // Default type
};

const char* kindToString(Kind k) {
    switch (k) {
    case Kind::Int32:  return "int32";
    case Kind::Int64:  return "int64";
    case Kind::Float:  return "float";
    case Kind::Double: return "double";
    case Kind::String: return "string";
    case Kind::Blob:   return "blob";
    }
    return "unknown";
}