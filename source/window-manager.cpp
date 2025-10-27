#include "window-manager.h"
#include "appstate.h"

// Include window headers here
#include "window-console.h"
#include "window-signalbrowser.h"
#include "window-filebrowser.h"
#include "window-fps.h"
#include "window-live.h"
#include "window-openproject.h"

WindowManager::WindowManager(AppState& state) : state_(state) {}

void WindowManager::subscribe(EventHandler handler) {
    handlers_.push_back(std::move(handler));
}

void WindowManager::publish(const WindowEvent& event) {
    for (auto& h : handlers_) {
        h(event);
    }
}

void WindowManager::openWindowByType(const std::string& type, const std::any& payload) {
    if (type == "window-console") {
        windows_.push_back(std::make_unique<Window_Console>(state_.console, &(state_.consoleIsOpen)));
    }
    else if (type == "window-signalbrowser") {
        windows_.push_back(std::make_unique<Window_SignalBrowser>(state_.projects[0].get()));
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