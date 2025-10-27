#pragma once

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AppState;
struct IWindow;

// Forward declarations of window classes
class Window_Console;
class Window_SignalBrowser;
class Window_FileBrowser;
class Window_FPS;
class Window_Live;
class Window_OpenProject;

enum class WindowEventType {
    OpenWindow,
    CloseWindow,
    DataUpdate
};

struct WindowEvent {
    WindowEventType type;
    std::string targetWindowType; // e.g. "DetailsWindow"
    std::any payload;             // Arbitrary data
};

class WindowManager {
public:
    WindowManager(AppState& state);

    using EventHandler = std::function<void(const WindowEvent&)>;

    void subscribe(EventHandler handler);
    void publish(const WindowEvent& event);
    void openWindowByType(const std::string& type, const std::any& payload = {});
    void closeWindow(IWindow* window);
    void renderAll();

private:
    std::vector<std::unique_ptr<IWindow>> windows_;
    std::vector<EventHandler> handlers_;
    AppState& state_;
};