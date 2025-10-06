#pragma once

#include "window.h"

class Window_OpenProject : public WindowCRTP<Window_OpenProject> {
public:
    Window_OpenProject() {
    }

    void OnDraw() {
        if (ImGui::Begin("Project Name")) {

            ImGui::End();
        }
    }
};