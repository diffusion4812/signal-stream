#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

class WindowManager;

struct IWindow {
    virtual ~IWindow() = default;
    virtual void Render(WindowManager& wm) = 0;
    virtual bool ShouldRemove() = 0;
};

template<typename Derived>
class WindowCRTP : public IWindow {
public:
    void Render(WindowManager& wm) {
        setFullscreen();
        static_cast<Derived*>(this)->OnRender(wm);
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

protected:
    bool fullscreen_ = false;
    bool shouldremove_ = false;
};