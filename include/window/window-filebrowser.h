#pragma once

#include "window.h"
#include "imfilebrowserplus.h"
#include "appstate.h"
#include "projectenvironment.h"

class Window_FileBrowser : public WindowCRTP<Window_FileBrowser> {
public:
    Window_FileBrowser(AppState * state)
        : state_(state) {
        dialog_.SetTitle("Open Project");
        dialog_.SetTypeFilters({ ".json" });
        dialog_.SetWindowSize(600, 400);
        dialog_.SetDirectory(std::filesystem::current_path());
        dialog_.Open();
    }

    void OnDraw() {
        dialog_.Display();
        if (dialog_.HasSelected()) {
            auto selectedPath = dialog_.GetSelected().string();
            std::string err;
            if (state_->projectManager->LoadProjectFromPath(selectedPath, false, err)) {
                state_->windows.push_back(std::make_unique<Window_SignalBrowser>(state_->projectManager));
            }
            dialog_.Close();
        }
    }

private:
    AppState* state_;
    ImGui::FileBrowser dialog_;
};