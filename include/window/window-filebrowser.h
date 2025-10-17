#pragma once

#include "window.h"
#define USE_WIN32_GETLOGICALDRIVES
#include "imfilebrowser.h"
#include "appstate.h"
#include "projectenvironment.h"
#include "hash.h"

class Window_FileBrowser : public WindowCRTP<Window_FileBrowser> {
public:
    Window_FileBrowser(AppState * state)
        : state_(state), dialog_(ImGuiFileBrowserFlags_CreateNewDir) {
        dialog_.SetTitle("Open Project");
        dialog_.SetTypeFilters({ ".json" });
        dialog_.SetWindowSize(600, 400);
        dialog_.SetDirectory(std::filesystem::current_path());

        dialog_.AddExtension(ImGui::FileBrowser::ExtensionSlot::Footer, [this](ImGui::FileBrowser& fb) {
            ImGui::Checkbox("Autostart", &autostart_);
            });

        dialog_.Open();
    }

    void OnDraw() {
        dialog_.Display();
        if (dialog_.HasSelected()) {
            auto selectedPath = dialog_.GetSelected().string();
            uint32_t selectedPathHash = fnv1a_32(selectedPath); // hash the path to identify already opened projects
            for (const auto& proj : state_->projects) {
                if (proj->GetHash() == selectedPathHash) {
                    ImGui::OpenPopup("Project Already Opened");
                    showModal_ = true;
                    if (ImGui::BeginPopupModal("Project Already Opened", &showModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("The project at %s is already opened.", selectedPath.c_str());
                        ImGui::EndPopup();
                    }
                    if (!showModal_) dialog_.Close(); // close the dialog if the modal is closed
                    return; // project already opened
                }
            }
            state_->projects.push_back(std::make_unique<ProjectManager>(selectedPath, autostart_));
            state_->windows.push_back(std::make_unique<Window_SignalBrowser>(state_->projects.back().get()));
            dialog_.Close();
        }
        if (!dialog_.IsOpened()) SetRemove();
    }

private:
    AppState* state_;
    ImGui::FileBrowser dialog_;
    bool showModal_ = false;
    bool autostart_ = false;
};