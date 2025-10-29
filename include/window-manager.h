#pragma once

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "service-bus.h"

struct AppState;
struct IWindow;

// Forward declarations of window classes
class Window_Console;
class Window_SignalBrowser;
class Window_FileBrowser;
class Window_FPS;
class Window_Live;
class Window_OpenProject;

class WindowManager {
public:
    struct WindowEvent {
        enum class Type { OpenWindow, CloseWindow, DataUpdate };
        Type type;
        std::string targetWindowType;
        std::any payload;
    };

    WindowManager(ServiceBus& bus, AppState& state);
    void openWindowByType(const std::string& type, const std::any& payload = {});
    IWindow* findWindowByType(const std::string& type);
    void closeWindow(IWindow* window);
    void renderAll();

private:
    ServiceBus& bus_;

    std::vector<std::unique_ptr<IWindow>> windows_;

    AppState& state_;
};