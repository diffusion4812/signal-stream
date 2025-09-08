#pragma once

#include <filesystem>
#include <SDL3/SDL.h>
#include "rapidcsv.h"
#include <deque>

typedef struct {
    std::filesystem::path filePath;
    bool fileIsRead;
    SDL_IOStream* fileStream;
    size_t fileSize;
    size_t bytesRead;
    rapidcsv::Document* parsedCsv;
    std::deque<std::deque<bool>> selectedAxis;
    bool csvTableWindowIsOpen;
    bool csvWindowIsOpen;
} CSVFile;

int SDLCALL ReadFileThread(void* userdata);
void prepAndReadFile(void* userdata, const char* filepath);