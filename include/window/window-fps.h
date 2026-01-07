#pragma once

#include "service-window.h"

namespace signal_stream {

    class Window_FPS : public WindowCRTP<Window_FPS> {
    public:
        Window_FPS(double* fps) : fps_(fps) {}
        void OnRender() {
            if (ImGui::Begin("FPS")) {
                ImGui::Text("%.2f", *fps_);
            }
            ImGui::End();
        }
    private:
        double* fps_;
    };

} // namespace signal_stream