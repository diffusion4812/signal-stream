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
                    SampleHandle sampleHandle;
                    bool validSample = svc->TryAcquireSample(sampleHandle, instance);

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
                            std::string val;
                            if (validSample) {
                                val = instance.get_as_string(field.name);
                            }

                            // Draw bullet
                            ImGui::Bullet();

                            // Create clickable/selectable text
                            std::string label = field.name + ": " + kindToString(field.kind) + " (" + val + ")";
                            ImGui::Selectable(label.c_str(), false);

                            // Double-click detection
                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                Window_Live::Payload payload = {
                                    *pm_,
                                    std::string(source.name)
                                };

                                wm.openWindowByType("window-live", payload);
                            }

                            // Drag-and-drop source
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                ImGui::SetDragDropPayload("FIELD_NAME", field.name.c_str(), field.name.size() + 1);
                                ImGui::Text("%s", field.name.c_str()); // Drag preview
                                ImGui::EndDragDropSource();
                            }

                            // Drag-and-drop target (optional)
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FIELD_NAME")) {
                                    const char* droppedField = (const char*)payload->Data;
                                    //handleDroppedField(droppedField);
                                }
                                ImGui::EndDragDropTarget();
                            }
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
