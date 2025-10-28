#pragma once

#include "window-manager.h"
#include "window-live.h"
#include "projectenvironment.h"
#include "imshape.h"

class Window_SignalBrowser : public WindowCRTP<Window_SignalBrowser>{
public:
    Window_SignalBrowser(ProjectManager* project) : pm_(project) {
    }

    void OnRender(WindowManager& wm) {
        if (ImGui::Begin(pm_->GetName().c_str())) {
            ImVec4 statusColor(0.7f, 0.7f, 0.7f, 1.0f);
            std::string statusText;

            if (ImGui::BeginTable("signal_list", 2, ImGuiTableFlags_SizingFixedFit)) {
                for (const auto& source : pm_->GetProjectData().sources) {
                    auto svc = pm_->GetService(source.name);
                    Instance instance(source.schema);

                    StreamBufferHandle handle = pm_->GetBufferHandle(source.name);
                    std::vector<std::byte> data = handle.get()->latest_parsed(1)[0].second; // Get the latest record (empty if no data available)
                    instance.set_data(data.data());

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    auto status = svc->Status();
                    switch (status) {
                        case SourceStatus::Running:
                            ImGui::ImShape::Circle(IM_COL32(50, 200, 50, 255));
                            break;
                        case SourceStatus::Stopped:
                            ImGui::ImShape::Square(IM_COL32(200, 200, 200, 255), 2.0f);
                            break;
                        default:
                            ImGui::ImShape::Triangle(IM_COL32(200, 200, 50, 255));
                            break;
                    }

                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::TreeNode(source.name.c_str())) {
                        ImGui::Text("Type: %s", source.type.c_str());

                        // Status column
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Status:");
                        ImGui::SameLine();
                        
                        if (!svc) {
                            statusText = "Not running";
                            statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        }
                        else {
                            auto status = svc->Status();
                            switch (status) {
                            case SourceStatus::Starting:
                                statusText = "Starting";
                                statusColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                                break;
                            case SourceStatus::Running:
                                statusText = "Running";
                                statusColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                                break;
                            case SourceStatus::Stopping:
                                statusText = "Stopping";
                                statusColor = ImVec4(0.8f, 0.5f, 0.2f, 1.0f);
                                break;
                            case SourceStatus::Stopped:
                                statusText = "Stopped";
                                statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                                break;
                            case SourceStatus::Error:
                                statusText = "Error";
                                statusColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
                                break;
                            default:
                                statusText = "Unknown";
                                statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                                break;
                            }
                        }
                        ImGui::TextColored(statusColor, "%s", statusText.c_str());

                        // Buttons: Start, Stop, Config
                        ImGui::SameLine();
                        std::string startBtn = "Start##" + source.name;
                        if (ImGui::Button(startBtn.c_str())) {
                            ProjectManager::ErrorString err;
                            pm_->StartService(source.name, err);
                            // Optionally handle error (e.g., show popup)
                        }
                        ImGui::SameLine();
                        std::string stopBtn = "Stop##" + source.name;
                        if (ImGui::Button(stopBtn.c_str())) {
                            ProjectManager::ErrorString err;
                            pm_->StopService(source.name, err);
                            // Optionally handle error
                        }
                        ImGui::SameLine();
                        std::string configBtn = "Config##" + source.name;
                        if (ImGui::Button(configBtn.c_str())) {
                            // TODO: Implement config dialog or callback
                        }

                        ImGui::Text("Last event: %s", svc->GetLastEvent()->message.c_str());

                        ImGui::ProgressBar(pm_->GetBufferHealth(source.name));

                        ImGui::Separator();
                        ImGui::Text("Schema:");
                        for (const auto& field : source.schema.get_fields()) {
                            std::string val = instance.get_as_string(field.name);

                            ImGui::PushID(source.name.c_str());
                            ImGui::PushID(field.name.c_str());

                            // Render selectable with a constant internal label
                            ImGui::Selectable("##signal_item", false);

                            // Handle double clicks
                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                wm.openWindowByType("window-live", std::any(Window_Live::Payload{
                                    *pm_,
                                    std::string(source.name),
                                    source.schema,
                                    field.name,
                                    field.kind
                                    }));
                            }

                            // Drag-drop source
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                Window_Live::DragAndDropPayload dd{ source.name, field.name, field.kind };
                                ImGui::SetDragDropPayload("SIGNAL", &dd, sizeof(dd));
                                ImGui::Text("%s", field.name.c_str());
                                ImGui::EndDragDropSource();
                            }

                            // Draw the live-updating text separately so it doesn't affect the ID
                            ImGui::SameLine();
                            ImGui::Text("%s: %s (%s)", field.name.c_str(), kindToString(field.kind), val.c_str());

                            ImGui::PopID(); // field
                            ImGui::PopID(); // source

                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }
    }

private:
    ProjectManager* pm_;

    enum HealthStatus {
        Healthy,
        Warning,
        Failure
    };
};
