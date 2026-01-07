#pragma once

#include "storage-buffer.h"
#include <vector>
#include <functional>
#include <algorithm>

namespace signal_stream {

    // Handles data extraction and transformation for plotting/visualization
    class PlotDataProvider {
    public:
        struct MultiPlotData {
            std::vector<double> xs;                   // shared timestamps
            std::vector<std::vector<double>> ys;      // one vector per signal

            size_t num_signals() const { return ys.size(); }
            size_t num_points() const { return xs.size(); }
            bool empty() const { return xs.empty(); }
        };

        // Extractor signature: takes payload bytes, writes to ys vectors
        // Mode 1 (push_back): extractor appends to each ys[i]
        // Mode 2 (overwrite): extractor writes to ys[i][0]
        using ExtractorFn = std::function<void(const std::byte*, std::vector<std::vector<double>>&)>;

        explicit PlotDataProvider(const StreamBuffer& buffer)
            : buffer_(buffer) {
        }

        // Get latest N records as plot data
        MultiPlotData latest_multi_plot_data(
            size_t n,
            size_t num_signals,
            ExtractorFn extractor) const;

        // Get time-range data with optional downsampling
        MultiPlotData range_multi_plot_data(
            ts_t start_ts,
            ts_t end_ts,
            size_t num_signals,
            ExtractorFn extractor,
            size_t plot_width_px = 0,      // 0 = no downsampling
            size_t max_points = 0) const;  // safety cap

    private:
        const StreamBuffer& buffer_;

        // Helper: extract record at logical index
        bool extract_record_at(
            size_t logical_idx,
            ts_t& out_timestamp,
            std::vector<std::byte>& out_payload) const;

        // Helper: downsample using min-max binning
        MultiPlotData downsample_minmax(
            const std::vector<ts_t>& timestamps,
            const std::vector<std::vector<std::byte>>& payloads,
            size_t num_signals,
            ExtractorFn extractor,
            size_t target_bins) const;
    };

} // namespace signal_stream