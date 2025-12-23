#pragma once

#include <filesystem>
#include <arrow/api.h>
#include <parquet/arrow/writer.h>
#include <arrow/io/file.h>

#include "service-storage.h"
#include "schema.h"
#include "instance.h"

#define timestamp_field_name "timestamp"

struct ParquetBackend : public StorageBackend {
    explicit ParquetBackend(const std::filesystem::path& basePath, const Schema& schema)
        : basePath_(basePath), schema_(schema) { // Copy schema
        if (!std::filesystem::exists(basePath_)) {
            std::filesystem::create_directories(basePath_);
        }
        initialise_builders(schema_);
        arrowSchema_ = create_arrow_schema();
        initialise_parquet_file("default_stream");
    }

    ~ParquetBackend() {
        if (filewriter_) {
            PARQUET_THROW_NOT_OK(filewriter_->Close());
        }
        if (outfile_) {
            PARQUET_THROW_NOT_OK(outfile_->Close());
		}
    }

    bool write_batch(const std::string& streamId, const std::vector<std::byte>& batch) override {
        std::lock_guard<std::mutex> lock(mtx_);

		Instance instance(schema_);
        const std::byte* payload_ptr = batch.data() + 8;
        instance.set_data(payload_ptr);

		// Append timestamp
		int64_t timestamp = *reinterpret_cast<const int64_t*>(batch.data());
		auto builder = std::static_pointer_cast<arrow::Int64Builder>(builders_[timestamp_field_name].builder);
		PARQUET_THROW_NOT_OK(builder->Append(timestamp));

        for (auto& field : schema_.fields()) {
            switch (field.kind) {
            case Kind::Int32: {
                int32_t value = instance.get<int32_t>(field.name).value_or(0);
                auto builder = std::static_pointer_cast<arrow::Int32Builder>(builders_[field.name].builder);
                PARQUET_THROW_NOT_OK(builder->Append(value));
                break;
            }
            case Kind::Int64: {
                int64_t value = instance.get<int64_t>(field.name).value_or(0);
                auto builder = std::static_pointer_cast<arrow::Int64Builder>(builders_[field.name].builder);
                PARQUET_THROW_NOT_OK(builder->Append(value));
                break;
            }
            case Kind::Float: { // float32
                float value = instance.get<float>(field.name).value_or(0.0);
                auto builder = std::static_pointer_cast<arrow::FloatBuilder>(builders_[field.name].builder);
                PARQUET_THROW_NOT_OK(builder->Append(value));
                break;
            }
            case Kind::Double: { // float64
                double value = instance.get<double>(field.name).value_or(0.0);
                auto builder = std::static_pointer_cast<arrow::DoubleBuilder>(builders_[field.name].builder);
                PARQUET_THROW_NOT_OK(builder->Append(value));
                break;
            }
            default:
                throw std::runtime_error("Unsupported Kind: " + std::string(kindToString(field.kind)));
            }
        }

        // Finish arrays
        std::vector<std::shared_ptr<arrow::Array>> arrays;
        arrays.reserve(builders_.size());
        for (auto& [name, sb] : builders_) {
            std::shared_ptr<arrow::Array> arr;
            PARQUET_THROW_NOT_OK(sb.builder->Finish(&arr));
            arrays.push_back(arr);
        }

        // Create table
        auto table = arrow::Table::Make(arrowSchema_, arrays);

        PARQUET_THROW_NOT_OK(filewriter_->WriteTable(*table, 1024));

        return true;
    }

private:
    struct SignalBuilder {
        std::shared_ptr<arrow::ArrayBuilder> builder;
        std::shared_ptr<arrow::Field> field; // includes name, type, and metadata
    };

    void initialise_builders(const Schema& schema) {
		auto builder = create_builder(Kind::Int64); // timestamp
		auto arrowfield = arrow::field(timestamp_field_name, to_arrow_type(Kind::Int64), false);
		builders_.insert({ timestamp_field_name, SignalBuilder{ builder, arrowfield } });
        for (const auto& field : schema.fields()) {
			if (field.name == timestamp_field_name) throw std::runtime_error("Field name reserved: " + std::string(timestamp_field_name));
			auto builder = create_builder(field.kind);
            auto arrowfield = arrow::field(field.name, to_arrow_type(field.kind), false);
			builders_.insert({ field.name, SignalBuilder{ builder, arrowfield } });
        }
    }

    std::shared_ptr<arrow::Schema> create_arrow_schema() const {
        std::vector<std::shared_ptr<arrow::Field>> fields;
        for (const auto& [name, sb] : builders_) {
            fields.push_back(sb.field);
        }
        return arrow::schema(fields);
    }

    std::shared_ptr<arrow::ArrayBuilder> create_builder(const Kind kind) {
        if (kind == Kind::Int32) return std::make_shared<arrow::Int32Builder>();
        if (kind == Kind::Int64) return std::make_shared<arrow::Int64Builder>();
        if (kind == Kind::Float) return std::make_shared<arrow::FloatBuilder>();
        if (kind == Kind::Double) return std::make_shared<arrow::DoubleBuilder>();
        throw std::runtime_error("Unsupported type: " + std::string(kindToString(kind)));
    }

    std::shared_ptr<arrow::DataType> to_arrow_type(const Kind kind) {
        if (kind == Kind::Int32) return arrow::int32();
        if (kind == Kind::Int64) return arrow::int64();
        if (kind == Kind::Float) return arrow::float32();
        if (kind == Kind::Double) return arrow::float64();
        throw std::runtime_error("Unsupported type: " + std::string(kindToString(kind)));
    }

    void initialise_parquet_file(const std::string& streamId) {
        std::filesystem::path filePath = basePath_ / (streamId + ".parquet");
        PARQUET_ASSIGN_OR_THROW(outfile_, arrow::io::FileOutputStream::Open(filePath.string()));

        PARQUET_ASSIGN_OR_THROW(filewriter_, parquet::arrow::FileWriter::Open(*arrowSchema_, arrow::default_memory_pool(), outfile_));
    }

    size_t calculate_record_size(const Schema& schema) {
        // TODO: Implement record size calculation based on schema
        return 0; // Placeholder
    }

    std::filesystem::path basePath_;
    const Schema& schema_;
	std::shared_ptr<arrow::Schema> arrowSchema_;
    std::mutex mtx_;
    std::unordered_map<std::string, SignalBuilder> builders_;
    std::shared_ptr<arrow::io::FileOutputStream> outfile_;
    std::shared_ptr<parquet::arrow::FileWriter> filewriter_;
};