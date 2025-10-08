#include "csv.h"

// Function to read file in a separate thread using SDL3 file IO
int SDLCALL ReadFileThread(void* userdata) {
    CSVFile* file = static_cast<CSVFile*>(userdata);

    // Parse the CSV file
    try {
        SDL_Log("Opening file: %s", file->filePath.filename().string().c_str());
        file->parsedCsv = new rapidcsv::Document(
            file->filePath.string().c_str(),
            rapidcsv::LabelParams(0, -1),
            rapidcsv::SeparatorParams(',', '\n'),
            rapidcsv::ConverterParams(),
            rapidcsv::LineReaderParams()
        );
    }
    catch (const std::exception& e) {
        SDL_Log("Error reading file '%s': %s", file->filePath.string(), e.what());
        return -1;
    }

    // Get column and row counts
    size_t colCount = file->parsedCsv->GetColumnCount();
    size_t rowCount = file->parsedCsv->GetRowCount();

    file->selectedAxis.resize(2);
    for (auto& axis : file->selectedAxis) {
        axis.resize(colCount, false);
    }

    // Log file info
    SDL_Log("File '%s' read successfully (%zu columns, %zu rows)", file->filePath.string().c_str(), colCount, rowCount);

    file->fileIsRead = true;
    file->csvWindowIsOpen = true;
    file->csvTableWindowIsOpen = false;
    return 0;
}

void prepAndReadFile(void* userdata, const char* filepath) {
    //AppState* state = (AppState*)userdata;

    SDL_Log("Full path to selected file: '%s'", filepath);

    /*state->csvFiles.push_back(CSVFile());
    CSVFile* file = &state->csvFiles.back(); // Get last added file
    file->filePath = std::filesystem::path(filepath);
    file->fileIsRead = false;
    file->fileStream = nullptr;
    file->fileSize = 0;
    file->bytesRead = 0;
    file->parsedCsv = nullptr;
    file->csvTableWindowIsOpen = false;
    file->csvWindowIsOpen = false;

    SDL_Thread* thread = SDL_CreateThread(ReadFileThread, NULL, file);
    SDL_DetachThread(thread);

    state->windows.push_back(std::make_unique<Window_Analysis>(file));*/
}
