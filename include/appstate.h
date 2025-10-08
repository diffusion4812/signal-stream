#pragma once

#include <boost/asio.hpp>

class Console;
class IWindow;
class ProjectManager;
class Schema;

struct AppState {
    Console* console;
    bool consoleIsOpen;

    bool show_debug_log;
    bool show_metrics_window;

    double fps;
    int64_t lastInputTimestamp;
    bool isIdle;
    uint64_t frameCount;
    uint64_t lastTime;


    boost::asio::io_context* io_context;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work;
    SDL_Thread* ioThread;

    Schema* schema;
    ProjectManager* projectManager;

    std::vector<std::unique_ptr<IWindow>> windows;
};