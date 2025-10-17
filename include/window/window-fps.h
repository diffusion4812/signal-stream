#pragma once

#include "window.h"

class Window_FPS : public WindowCRTP<Window_FPS> {
public:
    Window_FPS(double* fps) : fps_(fps) {}
    void OnDraw() {
        if (ImGui::Begin("FPS")) {
            ImGui::Text("%.2f", *fps_);
            ImGui::End();
        }
    }
private:
    double* fps_;
};