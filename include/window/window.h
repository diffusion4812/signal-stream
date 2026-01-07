#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

namespace signal_stream {

    class WindowManager;

    struct IWindow {
        virtual ~IWindow() = default;
        virtual void Render() = 0;
        virtual const std::string& GetId() const = 0;
        virtual void SetId(const std::string& id) = 0;
        virtual bool ShouldRemove() = 0;
    };

    template<typename Derived>
    class WindowCRTP : public IWindow {
    public:
        void Render() {
            setFullscreen();
            static_cast<Derived*>(this)->OnRender();
        }

        void SetRemove() {
            shouldremove_ = true;
        }

        bool ShouldRemove() {
            return shouldremove_;
        }

        const std::string& GetId() const { return id_; }
        void SetId(const std::string& id) { id_ = id; }

    private:
        void setFullscreen() {
            const ImGuiIO& io = ImGui::GetIO();
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            if (fullscreen_) {
                ImGui::SetNextWindowPos(vp->Pos);
                ImGui::SetNextWindowSize(vp->Size);
                ImGui::SetNextWindowViewport(vp->ID);
            }
        }

    protected:
        bool fullscreen_ = false;
        bool shouldremove_ = false;
        std::string id_ = "";
    };

} // namespace signal_stream