#pragma once

#include <imgui.h>

struct IWindow {
    virtual ~IWindow() = default;
    virtual void Draw() = 0;
    virtual bool ShouldRemove() = 0;
};

template<typename Derived>
class WindowCRTP : public IWindow {
public:
    void Draw() {
        setFullscreen();
        static_cast<Derived*>(this)->OnDraw();
    }

    void SetRemove() {
        shouldremove_ = true;
    }

    bool ShouldRemove() {
        return shouldremove_;
    }

private:
    void setFullscreen() {
        ImGuiIO& io = ImGui::GetIO();
        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (fullscreen_) {
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::SetNextWindowViewport(vp->ID);
        }
    }

    bool fullscreen_ = false;
    bool shouldremove_ = false;
};

//#include "window-analysis.h"
#include "window-console.h"
#include "window-signalbrowser.h"
#include "window-filebrowser.h"
#include "window-fps.h"
#include "window-live.h"
#include "window-openproject.h"
