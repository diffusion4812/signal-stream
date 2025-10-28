#pragma once

#include <implot.h>

#include "window-manager.h"
#include "projectenvironment.h"
#include "storage-manager.h"
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

    void OnRender(WindowManager& wm) {
        ImGui::Begin("Live Window");

        for (auto& source : sources_) {
            StreamBuffer::MultiPlotData plotData = source.second.handle->buf->latest_multi_plot_data(
                10000,
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
                            ys[i].push_back(static_cast<double>(instance_.get<float>(signal.name).value()));
                            break;
                        }
                    }
                }
            );

            ImPlot::BeginPlot(source.first.c_str());
            ImPlot::SetupAxis(ImAxis_X1, "Time");
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
            ImPlot::SetupAxis(ImAxis_Y1, "");
            ImPlot::SetupAxis(ImAxis_Y2, "");

            for (size_t i = 0; i < source.second.signals.size(); ++i) {
                ImPlot::SetAxis(source.second.signals[i].axis);
                ImPlot::PlotLine(source.second.signals[i].name.c_str(), plotData.xs.data(), plotData.ys[i].data(), plotData.xs.size());
            }

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
};