#pragma once

#include "window.h"

class Window_FPS : public WindowCRTP<Window_FPS> {
public:
    Window_FPS(double* fps) {
        mFPS = fps;
    }
    void OnDraw() {
        ImGui::Begin("FPS");
        ImGui::Text("%.2f", *mFPS);
        ImGui::End();
    }
private:
    double* mFPS;
};