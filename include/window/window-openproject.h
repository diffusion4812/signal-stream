#pragma once

#include "window-manager.h"

class Window_OpenProject : public WindowCRTP<Window_OpenProject> {
public:
    Window_OpenProject() {
    }

    void OnRender(WindowManager& wm) {
        if (ImGui::Begin("Project Name")) {

            ImGui::End();
        }
    }
};