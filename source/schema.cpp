#include "schema.h"

Kind kindFromString(std::string type_str) {
    if (type_str == "int32") return Kind::Int32;
    else if (type_str == "float64") return Kind::Float;
    else throw std::runtime_error("Unsupported type: " + type_str);  // Or return a default
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