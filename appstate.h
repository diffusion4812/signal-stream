#pragma once

#include "window.h"
#include "csv.h"
#include "console.h"
#include <boost/asio.hpp>
#include "tcpip.h"
#include "buffer.h"

typedef struct {
    Console* console;

    bool show_debug_log;
    bool show_metrics_window;

    double fps;
    int64_t lastInputTimestamp;
    bool isIdle;
    uint64_t frameCount;
    uint64_t lastTime;

    std::vector<CSVFile> csvFiles;
    bool consoleIsOpen;

    boost::asio::io_context* io_context;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work;
    SDL_Thread* ioThread;

    SPSC_CircularBuffer<int>* buffer_;

    std::vector<std::unique_ptr<Window>> windows;
    std::vector<std::unique_ptr<Server>> servers;
    std::vector<std::unique_ptr<Session>> sessions;
} AppState;