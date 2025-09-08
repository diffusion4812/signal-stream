#pragma once

#include "window.h"
#include "csv.h"
#include "console.h"
#include <boost/asio.hpp>
#include "tcpip.h"

typedef struct {
    Console* console;

    double fps;
    double* fpshistory;
    Uint64 frameCount;
    Uint64 lastTime;
    double frequency;

    std::vector<CSVFile> csvFiles;
    bool consoleIsOpen;

    boost::asio::io_context* io_context;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work;
    SDL_Thread* ioThread;

    Server* server;

    std::vector<std::unique_ptr<Window>> windows;
} AppState;