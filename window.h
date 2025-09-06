#pragma once

#include <imgui.h>
#include "implot.h"

#include "console.h"
#include "csv.h"

class Window {
public:
    virtual void draw();
};

class Window_Console : public Window {
public:
    Window_Console(Console* console, bool* consoleIsOpen);
    void draw();
private:
    Console* mConsole;
    bool* mConsoleIsOpen;
};

class Window_FPS : public Window {
public:
    Window_FPS(double* fps);
    void draw();
private:
    double* mFPS;
};

class Window_Analysis : public Window {
public:
    Window_Analysis(CSVFile* CSVFile);
    void draw();
private:
    CSVFile* mCSVFile;
};