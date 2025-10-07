#pragma once

#include "window.h"
#include "projectenvironment.h"

class Window_SignalBrowser : public WindowCRTP<Window_SignalBrowser>{
public:
    Window_SignalBrowser(ProjectManager* project) : pm_(project) {
    }

    void OnDraw() {
        if (ImGui::Begin("Signal Browser")) {
            for (const auto& source : pm_->GetProjectData().sources) {
                if (ImGui::TreeNode(source.name.c_str())) {
                    ImGui::Text("Type: %s", source.type.c_str());

                    // Status column
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Status:");
                    ImGui::SameLine();
                    std::string statusText;
                    ImVec4 statusColor(0.7f, 0.7f, 0.7f, 1.0f);
                    auto svc = pm_->GetService(source.name);
                    if (!svc) {
                        statusText = "Not running";
                        statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                    } else {
                        auto status = svc->Status();
                        switch (status) {
                        case ServiceStatus::Starting:
                            statusText = "Starting";
                            statusColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                            break;
                        case ServiceStatus::Running:
                            statusText = "Running";
                            statusColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
                            break;
                        case ServiceStatus::Stopping:
                            statusText = "Stopping";
                            statusColor = ImVec4(0.8f, 0.5f, 0.2f, 1.0f);
                            break;
                        case ServiceStatus::Stopped:
                            statusText = "Stopped";
                            statusColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                            break;
                        case ServiceStatus::Error:
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
                    
                    ImGui::Separator();
                    ImGui::Text("Schema:");
                    for (const auto& field : source.schema.get_fields()) {
                        ImGui::BulletText("%s: %s", field.name.c_str(), kindToString(field.kind));
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::End();
        }
    }

private:
    ProjectManager* pm_;
};
