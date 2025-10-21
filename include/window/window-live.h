#pragma once

#include <implot.h>

#include "window.h"
#include "projectenvironment.h"
#include "storage-manager.h"
#include "storage-buffer.h"

class Window_Live : public WindowCRTP<Window_Live> {
public:
    explicit Window_Live(ProjectManager* project) : pm_(project) {
    }

    void OnDraw() {
        ImGui::Begin("Live Window");
        auto svc = pm_->GetService("my random data");
        StreamBufferHandle buffer = pm_->GetBufferHandle("my random data");
        std::vector<std::pair<ts_t, std::vector<uint8_t>>> records = buffer.buf->latest_parsed(1000);
        size_t N = records.size();
        std::vector<double> xs;
        std::vector<double> ys;
        xs.reserve(N);
        ys.reserve(N);

        // choose reference time to show relative seconds (prevents large X values)
        double t0 = 0;
        if (N > 0) t0 = static_cast<double>(records.front().first) / 1000000000.0; // first ts in seconds

        for (const auto& rec : records) {
            int64_t ts_ms = rec.first;
            // X: relative time in seconds
            double t = static_cast<double>(ts_ms) / 1000000000.0 - t0;
            xs.push_back(t);

            // Y: decode payload -> here we assume 8-byte little-endian double
            const auto& payload = rec.second;
            std::int32_t value = 0.0;
            std::memcpy(&value, payload.data(), sizeof(std::int32_t));
            ys.push_back(static_cast<double>(value));
        }

        // now plot with ImPlot; must be inside ImGui/ImPlot frame and between BeginPlot/EndPlot
        // Example:
        ImPlot::BeginPlot("My Series");
        ImPlot::PlotLine("series", xs.data(), ys.data(), static_cast<int>(N));
        ImPlot::EndPlot();


        ImGui::End();
    }
private:
    static ImPlotPoint DataGetter(int idx, void* user_data) {
        return ImPlotPoint(0, 0);
    }

    ProjectManager* pm_;
};