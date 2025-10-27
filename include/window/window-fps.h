#pragma once

#include "window-manager.h"

class Window_FPS : public WindowCRTP<Window_FPS> {
public:
    Window_FPS(double* fps) : fps_(fps) {}
    void OnRender(WindowManager& wm) {
        if (ImGui::Begin("FPS")) {
            ImGui::Text("%.2f", *fps_);
            ImGui::End();
        }
    }
private:
    double* fps_;
};