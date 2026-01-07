#pragma once

#include "service-window.h"

namespace signal_stream {

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

} // namespace signal_stream