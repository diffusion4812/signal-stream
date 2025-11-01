#include "service-window.h"
#include "appstate.h"

// Include window headers here
#include "window-console.h"
#include "window-signalbrowser.h"
#include "window-filebrowser.h"
#include "window-fps.h"
#include "window-live.h"
#include "window-openproject.h"

WindowManager::WindowManager(ServiceBus& bus, AppState& state) :
    bus_(bus),
    state_(state) {
    bus_.Subscribe<WindowEvent>([&](const WindowEvent& ev) {
        // Handle incoming messages
    });
}

void WindowManager::openWindowByType(const std::string& type, const std::any& payload) {
    if (type == "window-console") {
        windows_.push_back(std::make_unique<Window_Console>(state_.console, &(state_.consoleIsOpen)));
    }
    else if (type == "window-signalbrowser") {
        windows_.push_back(std::make_unique<Window_SignalBrowser>(state_.pm.get()));
    }
    else if (type == "window-filebrowser") {
        windows_.push_back(std::make_unique<Window_FileBrowser>(&state_));
    }
    else if (type == "window-fps") {
        windows_.push_back(std::make_unique<Window_FPS>(&state_.fps));
    }
    else if (type == "window-live") {
        windows_.push_back(std::make_unique<Window_Live>(std::any_cast<Window_Live::Payload>(payload)));
    }
    else if (type == "window-openproject") {
        windows_.push_back(std::make_unique<Window_OpenProject>());
    }
}

IWindow* WindowManager::findWindowByType(const std::string& type) {
    for (const auto& wptr : windows_) {
        if (!wptr) continue;

        if (type == "window-console") {
            if (dynamic_cast<Window_Console*>(wptr.get())) return wptr.get();
        }
        else if (type == "window-signalbrowser") {
            if (dynamic_cast<Window_SignalBrowser*>(wptr.get())) return wptr.get();
        }
        else if (type == "window-filebrowser") {
            if (dynamic_cast<Window_FileBrowser*>(wptr.get())) return wptr.get();
        }
        else if (type == "window-fps") {
            if (dynamic_cast<Window_FPS*>(wptr.get())) return wptr.get();
        }
        else if (type == "window-live") {
            if (dynamic_cast<Window_Live*>(wptr.get())) return wptr.get();
        }
        else if (type == "window-openproject") {
            if (dynamic_cast<Window_OpenProject*>(wptr.get())) return wptr.get();
        }
    }
    return nullptr;
}

void WindowManager::closeWindow(IWindow* window) {
    windows_.erase(
        std::remove_if(windows_.begin(), windows_.end(),
            [&](const std::unique_ptr<IWindow>& w) { return w.get() == window; }),
        windows_.end()
    );
}

void WindowManager::renderAll() {
    for (auto& w : windows_) {
        w->Render(*this);
    }
    windows_.erase(
        std::remove_if(windows_.begin(), windows_.end(),
            [](const std::unique_ptr<IWindow>& w) { return w->ShouldRemove(); }),
        windows_.end()
    );
}