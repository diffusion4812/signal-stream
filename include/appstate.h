#pragma once

#include "spdlog/spdlog.h"
#include <boost/asio.hpp>
#include "projectenvironment.h"
#include "window.h"

class ServiceBus;
class Console;
class WindowManager;
class Schema;
struct SDL_Thread;

struct AppState {
    std::unique_ptr<ServiceBus> bus;
    std::unique_ptr<ProjectManager> pm;
    std::unique_ptr<WindowManager> wm;
    std::shared_ptr<spdlog::logger> log;

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
};