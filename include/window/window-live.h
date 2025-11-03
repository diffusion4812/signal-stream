#pragma once

#include <implot.h>

#include "service-window.h"
#include "service-project.h"
#include "service-storage.h"
#include "storage-buffer.h"
#include "schema.h"
#include "instance.h"

class Window_Live : public WindowCRTP<Window_Live> {
public:
    struct Payload {
        ProjectManager& pm;
        std::string sourcename;
        Schema schema;
        // First selected signal
        std::string signalname;
        Kind signalkind;
    };

    struct DragAndDropPayload {
        std::string sourcename;
        std::string signalname;
        Kind signalkind;
    };

    explicit Window_Live(const Payload& payload) :
        pm_(payload.pm),
        schema_(payload.schema),
        instance_(schema_) {

        DisplayedSource source;
        source.handle = std::make_unique<StreamBufferHandle>(pm_.GetBufferHandle(payload.sourcename));
        source.signals.emplace_back(DisplayedSignal{ payload.signalname, payload.signalkind });

        sources_.emplace(payload.sourcename, std::move(source));

        ImPlotStyle& style = ImPlot::GetStyle();
        style.Use24HourClock = true;
        style.UseLocalTime = true;
    }

    void OnRender() {
        if (ImGui::Begin("Live Window")) {
            for (auto& source : sources_) {
                StreamBuffer::MultiPlotData plotData = source.second.handle->buf->range_multi_plot_data(
                    60 * 1e9, // 30 seconds of data
                    0,        // Get latest x seconds
                    source.second.signals.size(),
                    [&](const std::byte* payload, std::vector<std::vector<double>>& ys) {
                        instance_.set_data(payload);
                        for (size_t i = 0; i < source.second.signals.size(); ++i) {
                            const auto& signal = source.second.signals[i];
                            switch (signal.kind) {
                            case Kind::Int32:
                                ys[i].push_back(static_cast<double>(instance_.get<int32_t>(signal.name).value()));
                                break;
                            case Kind::Float:
                                if (ys[i].size() == 1) {
                                    ys[i][0] = static_cast<double>(instance_.get<float>(signal.name).value()); // overwrite mode
                                }
                                else {
                                    ys[i].push_back(static_cast<double>(instance_.get<float>(signal.name).value())); // raw mode
                                }
                                break;
                            }
                        }
                    },
                    static_cast<size_t>(ImGui::GetContentRegionAvail().x),
                    5000
                );

                linked_max_ = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) / 1e9;
                linked_min_ = linked_max_ - duration_; // Show last 60 seconds

                ImPlot::BeginPlot(source.first.c_str(), ImVec2(-1, -1));
                ImPlot::SetupAxis(ImAxis_X1, "Time");
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
                ImPlot::SetupAxis(ImAxis_Y1, "");
                ImPlot::SetupAxis(ImAxis_Y2, "");
                ImPlot::SetupAxisLinks(ImAxis_X1, &linked_min_, &linked_max_);

                for (size_t i = 0; i < source.second.signals.size(); ++i) {
                    ImPlot::SetAxis(source.second.signals[i].axis);
                    ImPlot::PlotLine(source.second.signals[i].name.c_str(), plotData.xs.data(), plotData.ys[i].data(), plotData.xs.size());
                }

                ImPlotRect plot_limits = ImPlot::GetPlotLimits(ImAxis_X1, -1);
                duration_ = plot_limits.X.Max - plot_limits.X.Min;

                ImPlot::EndPlot();

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SIGNAL")) {
                        IM_ASSERT(payload->DataSize == sizeof(DragAndDropPayload));
                        const DragAndDropPayload* dragAndDropPayload = reinterpret_cast<const DragAndDropPayload*>(payload->Data);
                        DisplayedSignal signal;
                        signal.name = dragAndDropPayload->signalname;
                        signal.kind = dragAndDropPayload->signalkind;
                        source.second.signals.push_back(signal);
                    }
                }

            }
        }
        ImGui::End();
    }
private:
    static ImPlotPoint DataGetter(int idx, void* user_data) {
        return ImPlotPoint(0, 0);
    }

    struct DisplayedSignal {
        std::string name;
        Kind kind;
        ImAxis axis = ImAxis_Y1; // Default axis
    };

    struct DisplayedSource {
        std::unique_ptr<StreamBufferHandle> handle;
        std::vector<DisplayedSignal> signals;
    };

    std::unordered_map<std::string, DisplayedSource> sources_;

    ProjectManager& pm_;
    const Schema& schema_;
    Instance instance_;

    double linked_min_, linked_max_, duration_ = 60.0;
};