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
    TRACE_FUNCTION_SCOPE(bus_);
    token_ = bus_.Subscribe<Event>([&](const Event& ev) {
        switch (ev.type) {
        case Event::Type::OpenWindow:
            OpenWindow(ev.windowType, ev.payload);
            break;
        case Event::Type::DockWindow:
            DockWindow(std::any_cast<DockPayload>(ev.payload));
            break;
        case Event::Type::CloseWindow:
            CloseWindow(std::any_cast<ClosePayload>(ev.payload));
            break;
        }
    });
}

IWindow* WindowManager::OpenWindow(const std::string& type, const std::any& payload) {
    TRACE_FUNCTION_SCOPE(bus_);

    std::string uniqueId = GenerateUniqueId(type);

    if (windows_.find(uniqueId) != windows_.end()) {
        LOG_ERROR(bus_, "openWindow: Duplicate ID generated: " + uniqueId);
        return nullptr;
    }

    std::unique_ptr<IWindow> newWindow;

    if (type == "window-console") {
        newWindow = std::make_unique<Window_Console>(state_.console, &(state_.consoleIsOpen));
    }
    else if (type == "window-signalbrowser") {
        newWindow = std::make_unique<Window_SignalBrowser>(this, state_.pm.get());
    }
    else if (type == "window-filebrowser") {
        newWindow = std::make_unique<Window_FileBrowser>(&state_);
    }
    else if (type == "window-fps") {
        newWindow = std::make_unique<Window_FPS>(&state_.fps);
    }
    else if (type == "window-live") {
        newWindow = std::make_unique<Window_Live>(std::any_cast<Window_Live::Payload>(payload));
    }
    else if (type == "window-openproject") {
        newWindow = std::make_unique<Window_OpenProject>();
    }
    else {
        LOG_ERROR(bus_, "Undefined type: " + type);
        return nullptr;
    }

    newWindow->SetId(uniqueId);

    IWindow* ptr = newWindow.get();
    windows_.emplace(uniqueId, std::move(newWindow));

    LOG_INFO(bus_, "openWindow: Created window [" + uniqueId + "]");

    return ptr;
}

IWindow* WindowManager::FindWindowById(const std::string& id) {
    auto it = windows_.find(id);
    return (it != windows_.end()) ? it->second.get() : nullptr;
}

WindowManager::DockSpace& WindowManager::GetOrCreateDockSpace(const std::string& dockspaceId) {
    auto it = dockspaces_.find(dockspaceId);
    if (it == dockspaces_.end()) {
        DockSpace newSpace{ dockspaceId, {} };
        auto [insertedIt, _] = dockspaces_.emplace(dockspaceId, std::move(newSpace));
        return insertedIt->second;
    }
    return it->second;
}

void WindowManager::DockWindow(const DockPayload& payload) {
    TRACE_FUNCTION_SCOPE(bus_);
    IWindow* sourceWindow = FindWindowById(payload.sourceWindowId);
    IWindow* targetWindow = FindWindowById(payload.targetWindowId);

    if (!sourceWindow) {
        LOG_ERROR(bus_, "Source window not found: " + payload.sourceWindowId);
        return;
    }
    if (!targetWindow) {
LOG_ERROR(bus_, "Target window not found: " + payload.targetWindowId);        
        return;
    }

    // Determine dockspace ID based on target window
    std::string dockspaceId = targetWindow->GetId() + "_dockspace_" + payload.dockPosition;

    // Create dockspace if it doesn't exist
    DockSpace& dockspace = GetOrCreateDockSpace(dockspaceId);

    // Add target window if not already present
    if (std::find(dockspace.windows.begin(), dockspace.windows.end(), targetWindow) == dockspace.windows.end()) {
        dockspace.windows.push_back(targetWindow);
    }

    // Add source window to the dockspace
    dockspace.windows.push_back(sourceWindow);
    LOG_INFO(bus_, payload.sourceWindowId + " docked to " + payload.targetWindowId + " at " + payload.dockPosition);
}

void WindowManager::CloseWindow(const WindowManager::ClosePayload& payload) {
    TRACE_FUNCTION_SCOPE(bus_);
    std::erase_if(windows_, [&](auto& kv) {
        return kv.second->GetId() == payload.id;
    });
}

void WindowManager::RenderAll() {
    for (auto& kv : windows_) {
        kv.second->Render();
    }

    std::erase_if(windows_, [](auto& kv) {
        return kv.second->ShouldRemove();
    });
}