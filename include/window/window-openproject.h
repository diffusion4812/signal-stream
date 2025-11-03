#pragma once

#include "service-window.h"

class Window_OpenProject : public WindowCRTP<Window_OpenProject> {
public:
    Window_OpenProject() {
    }

    void OnRender() {
        if (ImGui::Begin("Project Name")) {

        }
        ImGui::End();
    }
};