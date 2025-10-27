#pragma once

#include <implot.h>

#include "window-manager.h"
#include "projectenvironment.h"
#include "storage-manager.h"
#include "storage-buffer.h"

class Window_Live : public WindowCRTP<Window_Live> {
public:
    struct Payload {
        ProjectManager& pm;
        std::string source;
    };

    explicit Window_Live(const Payload& payload) : pm_(payload.pm) {
        handle_ = std::make_unique<StreamBufferHandle>(pm_.GetBufferHandle(payload.source));
    }

    void OnRender(WindowManager& wm) {
        ImGui::Begin("Live Window");
        std::vector<std::pair<ts_t, std::vector<uint8_t>>> records = handle_->buf->latest_parsed(10000);
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
            float value = 0.0;
            std::memcpy(&value, payload.data(), sizeof(float));
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

    std::unique_ptr<StreamBufferHandle> handle_;
    ProjectManager& pm_;
};