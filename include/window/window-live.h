#pragma once

#include <implot.h>
#include <memory>
#include <unordered_map>
#include <chrono>

#include "service-window.h"
#include "service-project.h"
#include "service-storage.h"
#include "storage-buffer.h"
#include "plot-data-provider.h"
#include "schema.h"
#include "instance.h"

class Window_Live : public WindowCRTP<Window_Live> {
public:
    struct Payload {
        ProjectManager& pm;
        std::string sourcename;
        Schema schema;
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
        instance_(schema_),
        duration_(60.0),  // Default 60 seconds
        linked_min_(0.0),
        linked_max_(60.0)
    {
        // Initialize first source
        DisplayedSource source;
        source.handle = std::make_unique<StreamBufferHandle>(
            pm_.get_buffer_handle(payload.sourcename)
        );
        source.plotter = std::make_unique<PlotDataProvider>(
            *source.handle->buf
        );
        source.signals.emplace_back(DisplayedSignal{
            payload.signalname,
            payload.signalkind,
            ImAxis_Y1
            });

        sources_.emplace(payload.sourcename, std::move(source));

        // Configure ImPlot style
        ImPlotStyle& style = ImPlot::GetStyle();
        style.Use24HourClock = true;
        style.UseLocalTime = true;
    }

    void OnRender() {
        if (!ImGui::Begin("Live Window")) {
            ImGui::End();
            return;
        }

        // Update time window
        const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        linked_max_ = static_cast<double>(now_ns) / 1e9;
        linked_min_ = linked_max_ - duration_;

        // Render each source as a separate plot
        for (auto& [source_name, source] : sources_) {
            //RenderSourcePlot(source_name, source);
        }

        ImGui::End();
    }

private:
    struct DisplayedSignal {
        std::string name;
        Kind kind;
        ImAxis axis = ImAxis_Y1;
    };

    struct DisplayedSource {
        std::unique_ptr<StreamBufferHandle> handle;
        std::unique_ptr<PlotDataProvider> plotter;
        std::vector<DisplayedSignal> signals;
    };

    void RenderSourcePlot(const std::string& source_name, DisplayedSource& source) {
        if (source.signals.empty()) {
            return;
        }

        // Create extractor for this source's signals
        auto extractor = [this, &source](const std::byte* payload,
            std::vector<std::vector<double>>& ys) {
                instance_.set_data(payload);

                for (size_t i = 0; i < source.signals.size(); ++i) {
                    const auto& signal = source.signals[i];
                    double value = 0.0;

                    switch (signal.kind) {
                    case Kind::Int32: {
                        auto opt = instance_.get<int32_t>(signal.name);
                        value = opt ? static_cast<double>(*opt) : 0.0;
                        break;
                    }
                    case Kind::Int64: {
                        auto opt = instance_.get<int64_t>(signal.name);
                        value = opt ? static_cast<double>(*opt) : 0.0;
                        break;
                    }
                    case Kind::Float: {
                        auto opt = instance_.get<float>(signal.name);
                        value = opt ? static_cast<double>(*opt) : 0.0;
                        break;
                    }
                    case Kind::Double: {
                        auto opt = instance_.get<double>(signal.name);
                        value = opt ? *opt : 0.0;
                        break;
                    }
                    default:
                        value = 0.0;
                    }

                    // Push value to appropriate output vector
                    ys[i].push_back(value);
                }
            };

        // Get plot data with downsampling
        const ts_t duration_ns = static_cast<ts_t>(duration_ * 1e9);
        const size_t plot_width = static_cast<size_t>(
            std::max(100.0f, ImGui::GetContentRegionAvail().x)
            );

        PlotDataProvider::MultiPlotData plot_data = source.plotter->range_multi_plot_data(
            duration_ns,                    // Duration in nanoseconds
            0,                              // 0 = "last N seconds" mode
            source.signals.size(),          // Number of signals
            extractor,
            plot_width,                     // Downsample to plot width
            10000                           // Safety cap: max 10k points
        );

        // Render plot
        if (ImPlot::BeginPlot(source_name.c_str(), ImVec2(-1, 300))) {
            // Setup axes
            ImPlot::SetupAxis(ImAxis_X1, "Time");
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
            ImPlot::SetupAxis(ImAxis_Y1, "");
            ImPlot::SetupAxisLinks(ImAxis_X1, &linked_min_, &linked_max_);

            // Check if we need Y2 axis
            bool has_y2 = false;
            for (const auto& signal : source.signals) {
                if (signal.axis == ImAxis_Y2) {
                    has_y2 = true;
                    break;
                }
            }

            if (has_y2) {
                ImPlot::SetupAxis(ImAxis_Y2, "", ImPlotAxisFlags_AuxDefault);
            }

            // Plot each signal
            for (size_t i = 0; i < source.signals.size() && i < plot_data.ys.size(); ++i) {
                const auto& signal = source.signals[i];

                if (plot_data.ys[i].empty()) {
                    continue;
                }

                ImPlot::SetAxis(signal.axis);
                ImPlot::PlotLine(
                    signal.name.c_str(),
                    plot_data.xs.data(),
                    plot_data.ys[i].data(),
                    static_cast<int>(plot_data.num_points())
                );
            }

            // Update duration from user interaction (zoom/pan)
            ImPlotRect plot_limits = ImPlot::GetPlotLimits(ImAxis_X1, -1);
            duration_ = plot_limits.X.Max - plot_limits.X.Min;

            ImPlot::EndPlot();
        }

        // Handle drag-and-drop to add signals
        HandleDragDrop(source);

        // Show signal list with context menu
        RenderSignalList(source);
    }

    void HandleDragDrop(DisplayedSource& source) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SIGNAL")) {
            if (payload->DataSize == sizeof(DragAndDropPayload)) {
                const auto* drop_data = static_cast<const DragAndDropPayload*>(
                    payload->Data
                    );

                // Check if signal already exists
                auto it = std::find_if(
                    source.signals.begin(),
                    source.signals.end(),
                    [&](const DisplayedSignal& s) {
                        return s.name == drop_data->signalname;
                    }
                );

                if (it == source.signals.end()) {
                    DisplayedSignal new_signal{
                        drop_data->signalname,
                        drop_data->signalkind,
                        ImAxis_Y1  // Default to left axis
                    };
                    source.signals.push_back(new_signal);
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    void RenderSignalList(DisplayedSource& source) {
        if (!ImGui::TreeNode("Signals")) {
            return;
        }

        int to_remove = -1;

        for (size_t i = 0; i < source.signals.size(); ++i) {
            auto& signal = source.signals[i];

            ImGui::PushID(static_cast<int>(i));

            // Color indicator
            ImGui::ColorButton(
                "##color",
                ImPlot::GetColormapColor(static_cast<int>(i)),
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                ImVec2(20, 20)
            );
            ImGui::SameLine();

            // Collapsing header with signal info
            bool node_open = ImGui::TreeNodeEx(
                signal.name.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth
            );

            // Context menu
            if (ImGui::BeginPopupContextItem()) {
                ImGui::TextUnformatted("Axis Assignment:");
                ImGui::Separator();

                if (ImGui::MenuItem("Left Axis (Y1)", nullptr, signal.axis == ImAxis_Y1)) {
                    signal.axis = ImAxis_Y1;
                }

                if (ImGui::MenuItem("Right Axis (Y2)", nullptr, signal.axis == ImAxis_Y2)) {
                    signal.axis = ImAxis_Y2;
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Remove Signal", "Del")) {
                    to_remove = static_cast<int>(i);
                }

                ImGui::EndPopup();
            }

            if (node_open) {
                // Show signal details
                ImGui::Text("Type: %s", kind_to_string(signal.kind));
                ImGui::Text("Axis: %s", signal.axis == ImAxis_Y1 ? "Y1 (Left)" : "Y2 (Right)");

                // Could add statistics here if available
                // ImGui::Text("Min: %.3f", signal.stats.min);
                // ImGui::Text("Max: %.3f", signal.stats.max);
                // ImGui::Text("Avg: %.3f", signal.stats.avg);

                ImGui::TreePop();
            }

            // Drag to reorder
            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                int i_next = static_cast<int>(i) + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
                if (i_next >= 0 && i_next < static_cast<int>(source.signals.size())) {
                    std::swap(source.signals[i], source.signals[i_next]);
                    ImGui::ResetMouseDragDelta();
                }
            }

            ImGui::PopID();
        }

        // Remove signal after iteration
        if (to_remove >= 0 && to_remove < static_cast<int>(source.signals.size())) {
            source.signals.erase(source.signals.begin() + to_remove);
        }

        ImGui::TreePop();
    }

    // Helper function to convert Kind to string
    static const char* kind_to_string(Kind kind) {
        switch (kind) {
        case Kind::Int32:  return "int32";
        case Kind::Int64:  return "int64";
        case Kind::Float:  return "float";
        case Kind::Double: return "double";
        default:           return "unknown";
        }
    }

    void RenderTimeControls() {
        ImGui::SetNextItemWidth(150.0f);

        const char* preset_labels[] = { "5s", "10s", "30s", "1m", "5m", "10m", "30m", "1h" };
        const double preset_values[] = { 5.0, 10.0, 30.0, 60.0, 300.0, 600.0, 1800.0, 3600.0 };

        if (ImGui::BeginCombo("Duration", nullptr)) {
            for (int i = 0; i < IM_ARRAYSIZE(preset_labels); ++i) {
                if (ImGui::Selectable(preset_labels[i])) {
                    duration_ = preset_values[i];
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);

        float duration_f = static_cast<float>(duration_);
        if (ImGui::DragFloat("##custom_duration", &duration_f, 1.0f, 1.0f, 3600.0f, "%.1fs")) {
            duration_ = static_cast<double>(duration_f);
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset Zoom")) {
            duration_ = 60.0;
        }
    }

    std::unordered_map<std::string, DisplayedSource> sources_;

    ProjectManager& pm_;
    const Schema& schema_;
    Instance instance_;

    double linked_min_;
    double linked_max_;
    double duration_;
};