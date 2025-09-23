#pragma once

#include "window.h"
#include "csv.h"
#include "console.h"
#include <boost/asio.hpp>
#include "tcpip.h"
#include "buffer.h"
#include "schema.h"

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

    Schema* schema;
    SPSC_CircularBuffer<std::byte*>* buffer;

    std::vector<std::unique_ptr<IWindow>> windows;
    std::vector<std::unique_ptr<Server>> servers;
    std::vector<std::unique_ptr<Session>> sessions;
} AppState;