#pragma once

#include "window.h"

class Window_Live : public WindowCRTP<Window_Live> {
public:
    explicit Window_Live() {
    }

    void OnDraw() {
        ImGui::Begin("Live Window");
        ImGui::End();
    }
private:
    static ImPlotPoint DataGetter(int idx, void* user_data) {
        return ImPlotPoint(0, 0);
    }
};