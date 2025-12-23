#pragma once

#include <filesystem>
#include <arrow/api.h>
#include <parquet/arrow/writer.h>
#include <arrow/io/file.h>

#include "service-storage.h"
#include "schema.h"
#include "instance.h"

#define timestamp_field_name "timestamp"

struct ParquetBackend : public IStorageBackend {
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

    bool write_batch_two_pass(const std::string& streamId,
        const StreamBuffer::BatchChunks& chunks) {
        std::lock_guard<std::mutex> lock(mtx_);

        if (chunks.total_count == 0) return true;

        // Create Arrow array from contiguous chunk (created by SignalBuffer)
        auto create_arrow_array = [](const ISignalBuffer::ContiguousChunk& chunk)
            -> std::shared_ptr<arrow::Array> {

            auto buffer = arrow::Buffer::Wrap(reinterpret_cast<const std::uint8_t*>(chunk.data), chunk.size_bytes());

            switch (chunk.kind) {
            case Kind::Int32:
                return std::make_shared<arrow::Int32Array>(
                    chunk.count,          // length
                    buffer,               // data buffer
                    nullptr,              // null bitmap (none)
                    0                     // null count
                );
            case Kind::Int64:
                return std::make_shared<arrow::Int64Array>(chunk.count, buffer, nullptr, 0);
            case Kind::Float:
                return std::make_shared<arrow::FloatArray>(chunk.count, buffer, nullptr, 0);
            case Kind::Double:
                return std::make_shared<arrow::DoubleArray>(chunk.count, buffer, nullptr, 0);
            default:
                throw std::runtime_error("Unsupported type");
            }
        };

        // PASS 1: Write first contiguous chunk
        {
            std::vector<std::shared_ptr<arrow::Array>> arrays;
            arrays.reserve(chunks.first_chunk.size()); // Reserve space for all columns (fields in the schema)

            // Data columns (maintain schema order)
            for (const auto& field : schema_.fields()) {
                auto first_chunk = chunks.first_chunk.at(field.idx); // Locate fields through index
                if (!first_chunk.is_valid) {
                    throw std::runtime_error("Missing field in first chunk: " + field.name);
                }
				arrays.push_back(create_arrow_array(first_chunk));
            }

            auto batch1 = arrow::RecordBatch::Make(
                arrowSchema_,
                chunks.first_chunk.begin()->second.count,
                arrays
            );

            PARQUET_THROW_NOT_OK(filewriter_->WriteRecordBatch(*batch1));
        }

        // PASS 2: Write wrapped chunk if present
        if (chunks.has_wrap()) {
            std::vector<std::shared_ptr<arrow::Array>> arrays;
            arrays.reserve(chunks.second_chunk.size());

            // Data columns (maintain schema order)
            for (const auto& field : schema_.fields()) {
                auto second_chunk = chunks.second_chunk.at(field.idx); // Locate fields through index
                if (!second_chunk.is_valid) {
                    throw std::runtime_error("Missing field in first chunk: " + field.name);
                }
                arrays.push_back(create_arrow_array(second_chunk));
            }

            auto batch2 = arrow::RecordBatch::Make(
                arrowSchema_,
                chunks.second_chunk.begin()->second.count,
                arrays
            );

            PARQUET_THROW_NOT_OK(filewriter_->WriteRecordBatch(*batch2));
        }

        return true;
    }

private:
    struct SignalBuilder {
        std::shared_ptr<arrow::ArrayBuilder> builder;
        std::shared_ptr<arrow::Field> field; // includes name, type, and metadata
    };

    void initialise_builders(const Schema& schema) {
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