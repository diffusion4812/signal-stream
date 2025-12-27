#include "plot-data-provider.h"
#include <cstring>

PlotDataProvider::MultiPlotData
PlotDataProvider::latest_multi_plot_data(
    size_t n,
    size_t num_signals,
    ExtractorFn extractor) const
{
    MultiPlotData mpd;
    mpd.ys.resize(num_signals);

    if (n == 0 || buffer_.empty()) {
        return mpd;
    }

    const size_t current_size = buffer_.size();
    const size_t take = std::min(n, current_size);
    const size_t start_idx = current_size - take;

    mpd.xs.reserve(take);
    for (auto& y : mpd.ys) {
        y.reserve(take);
    }

    ts_t timestamp;
    std::vector<std::byte> payload(buffer_.record_size());

    for (size_t i = 0; i < take; ++i) {
        if (extract_record_at(start_idx + i, timestamp, payload)) {
            mpd.xs.push_back(static_cast<double>(timestamp) / 1e9);
            extractor(payload.data(), mpd.ys);
        }
    }

    return mpd;
}

PlotDataProvider::MultiPlotData
PlotDataProvider::range_multi_plot_data(
    ts_t start_ts,
    ts_t end_ts,
    size_t num_signals,
    ExtractorFn extractor,
    size_t plot_width_px,
    size_t max_points) const
{
    MultiPlotData mpd;
    mpd.ys.resize(num_signals);

    if (buffer_.empty()) {
        return mpd;
    }

    const size_t current_size = buffer_.size();

    // Handle "previous N seconds" mode (end_ts == 0)
    ts_t range_start, range_end;
    if (end_ts == 0) {
        ts_t latest_ts = 0;
        std::vector<std::byte> dummy_payload(buffer_.record_size());

        if (extract_record_at(current_size - 1, latest_ts, dummy_payload)) {
            range_start = latest_ts - start_ts;  // start_ts is duration in ns
            range_end = latest_ts;
        }
        else {
            return mpd;
        }
    }
    else {
        range_start = start_ts;
        range_end = end_ts;
    }

    if (range_start >= range_end) {
        return mpd;
    }

    // Binary search for first record >= range_start
    size_t low = 0, high = current_size;
    while (low < high) {
        size_t mid = (low + high) / 2;
        ts_t mid_ts;
        std::vector<std::byte> temp_payload(buffer_.record_size());

        if (extract_record_at(mid, mid_ts, temp_payload)) {
            if (mid_ts < range_start) {
                low = mid + 1;
            }
            else {
                high = mid;
            }
        }
        else {
            break;
        }
    }
    size_t first_idx = low;

    // Collect all timestamps and payloads in range
    std::vector<ts_t> timestamps;
    std::vector<std::vector<std::byte>> payloads;
    timestamps.reserve(current_size - first_idx);
    payloads.reserve(current_size - first_idx);

    ts_t ts;
    std::vector<std::byte> payload(buffer_.record_size());

    for (size_t i = first_idx; i < current_size; ++i) {
        if (!extract_record_at(i, ts, payload)) {
            break;
        }

        if (ts > range_end) {
            break;
        }

        timestamps.push_back(ts);
        payloads.push_back(payload);
    }

    const size_t sample_count = timestamps.size();
    if (sample_count == 0) {
        return mpd;
    }

    // Apply safety cap
    size_t effective_plot_width = plot_width_px;
    if (max_points > 0 && sample_count > max_points) {
        effective_plot_width = max_points / 2;  // each bin -> 2 points (min/max)
    }

    // RAW MODE: No downsampling needed
    if (effective_plot_width == 0 || sample_count <= effective_plot_width) {
        mpd.xs.reserve(sample_count);
        for (auto& y : mpd.ys) {
            y.reserve(sample_count);
        }

        for (size_t i = 0; i < sample_count; ++i) {
            mpd.xs.push_back(static_cast<double>(timestamps[i]) / 1e9);
            extractor(payloads[i].data(), mpd.ys);
        }

        return mpd;
    }

    // BINNING MODE
    return downsample_minmax(timestamps, payloads, num_signals,
        extractor, effective_plot_width);
}

PlotDataProvider::MultiPlotData
PlotDataProvider::downsample_minmax(
    const std::vector<ts_t>& timestamps,
    const std::vector<std::vector<std::byte>>& payloads,
    size_t num_signals,
    ExtractorFn extractor,
    size_t target_bins) const
{
    MultiPlotData mpd;
    mpd.ys.resize(num_signals);

    if (timestamps.empty() || target_bins == 0) {
        return mpd;
    }

    const ts_t range_start = timestamps.front();
    const ts_t range_end = timestamps.back();
    const ts_t bin_size_ns = (range_end - range_start) / target_bins;

    if (bin_size_ns == 0) {
        // Fallback: all timestamps identical
        mpd.xs.push_back(static_cast<double>(range_start) / 1e9);
        extractor(payloads[0].data(), mpd.ys);
        return mpd;
    }

    // Preallocate for estimated output size (2 points per bin)
    const size_t estimated_points = target_bins * 2;
    mpd.xs.reserve(estimated_points);
    for (auto& y : mpd.ys) {
        y.reserve(estimated_points);
    }

    // Temporary extraction buffer (overwrite mode: extractor writes to [0])
    std::vector<std::vector<double>> temp_ys(num_signals, std::vector<double>(1));
    std::vector<double> current_values(num_signals);
    std::vector<double> min_vals(num_signals);
    std::vector<double> max_vals(num_signals);

    ts_t bin_start = (range_start / bin_size_ns) * bin_size_ns;
    ts_t bin_end = bin_start + bin_size_ns;
    ts_t min_ts = 0, max_ts = 0;
    bool bin_has_data = false;

    // Lambda to push a point (timestamp + all signal values)
    auto push_point = [&](ts_t point_ts, const std::vector<double>& vals) {
        mpd.xs.push_back(static_cast<double>(point_ts) / 1e9);
        for (size_t s = 0; s < num_signals; ++s) {
            mpd.ys[s].push_back(vals[s]);
        }
        };

    // Lambda to flush current bin
    auto flush_bin = [&]() {
        if (!bin_has_data) return;

        // Output min and max in temporal order
        if (min_ts <= max_ts) {
            push_point(min_ts, min_vals);
            push_point(max_ts, max_vals);
        }
        else {
            push_point(max_ts, max_vals);
            push_point(min_ts, min_vals);
        }

        bin_has_data = false;
        };

    // Process all samples
    for (size_t i = 0; i < timestamps.size(); ++i) {
        const ts_t ts = timestamps[i];
        const std::byte* payload_ptr = payloads[i].data();

        // Move to next bin if needed
        while (ts >= bin_end) {
            flush_bin();
            bin_start = bin_end;
            bin_end += bin_size_ns;
        }

        // Extract values using overwrite mode
        extractor(payload_ptr, temp_ys);

        // Copy extracted values to current_values
        for (size_t s = 0; s < num_signals; ++s) {
            current_values[s] = temp_ys[s][0];
        }

        // Update bin statistics
        if (!bin_has_data) {
            // First sample in this bin
            min_vals = current_values;
            max_vals = current_values;
            min_ts = ts;
            max_ts = ts;
            bin_has_data = true;
        }
        else {
            // Update min/max for each signal
            for (size_t s = 0; s < num_signals; ++s) {
                if (current_values[s] < min_vals[s]) {
                    min_vals[s] = current_values[s];
                    min_ts = ts;
                }
                if (current_values[s] > max_vals[s]) {
                    max_vals[s] = current_values[s];
                    max_ts = ts;
                }
            }
        }
    }

    // Flush final bin
    flush_bin();

    return mpd;
}

// Helper: Extract a single record at logical index
bool PlotDataProvider::extract_record_at(
    size_t logical_idx,
    ts_t& out_timestamp,
    std::vector<std::byte>& out_payload) const
{
    if (logical_idx >= buffer_.size()) {
        return false;
    }

    // Get batch chunks for single record
    StreamBuffer::BatchChunks batch_chunks = buffer_.get_batch_chunks(logical_idx + 1);

    if (batch_chunks.total_count <= logical_idx) {
        return false;
    }

    const auto& schema = buffer_.get_schema();
    out_payload.resize(buffer_.record_size());

    // Reconstruct record from column buffers
    for (const auto& field : schema.fields()) {
        const ISignalBuffer::ContiguousChunk* source_chunk = nullptr;
        size_t chunk_local_idx = logical_idx;

        // Determine which chunk contains this record
        auto it_first = batch_chunks.first_chunk.find(field.idx);
        if (it_first != batch_chunks.first_chunk.end() &&
            logical_idx < it_first->second.count) {
            source_chunk = &it_first->second;
        }
        else {
            auto it_second = batch_chunks.second_chunk.find(field.idx);
            if (it_second != batch_chunks.second_chunk.end()) {
                source_chunk = &it_second->second;
                chunk_local_idx = logical_idx - it_first->second.count;
            }
        }

        if (source_chunk && source_chunk->is_valid) {
            const std::byte* src = source_chunk->data +
                chunk_local_idx * field.size;
            std::memcpy(out_payload.data() + field.offset, src, field.size);
        }
    }

    // Extract timestamp (assume first Int64 field at offset 0)
    const FieldDesc* ts_field = nullptr;
    for (const auto& field : schema.fields()) {
        if (field.kind == Kind::Int64 && field.offset == 0) {
            ts_field = &field;
            break;
        }
    }

    if (ts_field) {
        std::memcpy(&out_timestamp,
            out_payload.data() + ts_field->offset,
            sizeof(ts_t));
    }
    else {
        out_timestamp = 0;
    }

    return true;
}