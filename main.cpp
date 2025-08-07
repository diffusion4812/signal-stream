#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <implot.h>

#include <iostream>
#include <string>

#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_thread.h>

#include "rapidcsv.h"
#include "console.h"

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_GPUDevice* gpu_device = NULL;
static ImGuiIO io;

typedef struct {
    Console* console;

    double fps;
    double* fpshistory;
    Uint64 frameCount;
    Uint64 lastTime;
    double frequency;

    std::string filePath;
    bool fileIsRead;
    SDL_IOStream* fileStream;
    size_t fileSize;
    size_t bytesRead;
    char* fileBuffer;
    rapidcsv::Document parsedCsv;
	bool csvWindowIsOpen;
} AppState;

static Console* console = nullptr;

// Function to read file in a separate thread using SDL3 file IO
static int SDLCALL ReadFileThread(void* userdata) {
    AppState* state = static_cast<AppState*>(userdata);

    // Parse the CSV file
    try {
        state->parsedCsv.Load(state->filePath,
            rapidcsv::LabelParams(0, -1),
            rapidcsv::SeparatorParams(',', '\n'),
            rapidcsv::ConverterParams(),
            rapidcsv::LineReaderParams());
    }
    catch (const std::exception& e) {
        SDL_Log("Error reading file '%s': %s", state->filePath.c_str(), e.what());
        return -1; // Indicate failure
	}

    // Get column and row counts
    size_t colCount = state->parsedCsv.GetColumnCount();
    size_t rowCount = state->parsedCsv.GetRowCount();

    // Log file info
    SDL_Log("File '%s' read successfully (%zu columns, %zu rows)", state->filePath.c_str(), colCount, rowCount);

    state->fileIsRead = true;
    return 0;
}

static void SDLCALL callback(void* userdata, const char* const* filelist, int filter) {
    AppState* state = (AppState*)userdata;

    if (!filelist) {
        SDL_Log("An error occured: %s", SDL_GetError());
        return;
    }
    else if (!*filelist) {
        SDL_Log("The user did not select any file.");
        SDL_Log("Most likely, the dialog was canceled.");
        return;
    }

    if (*filelist) {
        SDL_Log("Full path to selected file: '%s'", *filelist);
        state->filePath = *filelist;
		state->fileIsRead = false;
        SDL_Thread* thread = SDL_CreateThread(ReadFileThread, "FileReader", state);
    }
}

static void SDLCALL appSDL_LogOutputFunction(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    AppState* state = (AppState*)userdata;
    state->console->log((int)priority, std::string(message));
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    AppState* state = new AppState();
    if (!state) {
        SDL_Log("Failed to allocate memory for app state: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    *appstate = state;

    console = new Console();
    if (!console) {
        SDL_Log("Failed to allocate memory for console: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    state->console = console;

    SDL_SetLogOutputFunction(appSDL_LogOutputFunction, state);

    // Setup SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) != 0)
    {
        SDL_Log("Error: SDL_Init(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create SDL window graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    window = SDL_CreateWindow("Dear ImGui SDL3+SDL_GPU example", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
    {
        SDL_Log("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create GPU Device
    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB, true, nullptr);
    if (gpu_device == nullptr)
    {
        SDL_Log("Error: SDL_CreateGPUDevice(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Claim window for GPU Device
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window))
    {
        SDL_Log("Error: SDL_ClaimWindowForGPUDevice(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
	ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	io.IniFilename = NULL; // Disable ini file saving

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = gpu_device;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&init_info);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    switch (event->type) {
    case SDL_EVENT_QUIT:
        SDL_Log("Received quit event.");
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    AppState* state = (AppState*)appstate;
    io = ImGui::GetIO();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(ImGuiDockNodeFlags_PassthruCentralNode); // ImGuiDockNodeFlags_PassthruCentralNode allows content to be drawn behind the dockspace

    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open CSV")) {
            // Handle file open dialog here
            SDL_Log("Open CSV file dialog would be implemented here.");
            SDL_ShowOpenFileDialog(callback, appstate, nullptr, nullptr, 0, nullptr, false);
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();

    ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_Once); // ImGuiCond_Once prevents it from re-docking every frame if the user moves it

    {
        ImGui::Begin("Console");
        if (ImGui::Button("Clear Console")) {
            console->removeAll();
        }
        ImGui::SameLine();
        ImGui::Text("Log Count: %d", console->getCount());
        ImGui::Text("Console Output:");

        // Display each log entry with context menu
        for (int i = 0; i < console->getCount(); ++i) {
            auto item = console->getItem(i);
            ImGui::PushID(i);
            if (ImGui::Selectable(item.text.c_str())) {
                // Optionally handle selection
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Copy")) {
                    ImGui::SetClipboardText(item.text.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    if (state->filePath != "") {
        ImGui::Begin("CSV Plot", &state->csvWindowIsOpen);
        ImGui::Text("File Path: %s", state->filePath.c_str());
        
        // Show a table with the first few rows of the CSV file
        if (state->fileIsRead) {
            size_t colCount = state->parsedCsv.GetColumnCount();
            size_t rowCount = state->parsedCsv.GetRowCount();
            size_t previewRows = (rowCount < 15) ? rowCount : 10;

            if (ImGui::BeginTable("CSVTable", static_cast<int>(colCount), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                // Header row
                ImGui::TableNextRow();
                for (size_t col = 0; col < colCount; ++col) {
                    ImGui::TableSetColumnIndex(static_cast<int>(col));
                    ImGui::TextUnformatted(state->parsedCsv.GetColumnName(col).c_str());
                }
                // Data rows
                for (size_t row = 0; row < previewRows; ++row) {
                    ImGui::TableNextRow();
                    for (size_t col = 0; col < colCount; ++col) {
                        ImGui::TableSetColumnIndex(static_cast<int>(col));
                        try {
                            ImGui::TextUnformatted(state->parsedCsv.GetCell<std::string>(col, row).c_str());
                        } catch (...) {
                            ImGui::TextUnformatted("");
                        }
                    }
                }
                ImGui::EndTable();
                if (rowCount > previewRows) {
                    ImGui::Text("... (%zu more rows)", rowCount - previewRows);
                }
            }
        }
        else {
            ImGui::Text("Loading file...");
		}
        ImGui::End();
    }

    
    // --- ImPlot CSV Plotting ---
    if (state->filePath != "" && state->fileIsRead && state->parsedCsv.GetColumnCount() > 1) {
        ImGui::Begin("CSV Plot", &state->csvWindowIsOpen);
        ImGui::Text("File Path: %s", state->filePath.c_str());

        // Plotting section
        static int xCol = 0;
        static int yCol = 1;
        size_t colCount = state->parsedCsv.GetColumnCount();
        size_t rowCount = state->parsedCsv.GetRowCount();
        size_t plotRows = (rowCount < 1000000) ? rowCount : 1000; // Limit for performance

        // Column selection
        ImGui::Separator();
        ImGui::Text("Select columns to plot:");
        ImGui::PushID("xcol");
        if (ImGui::BeginCombo("X Axis", state->parsedCsv.GetColumnName(xCol).c_str())) {
            for (size_t i = 0; i < colCount; ++i) {
                bool selected = (xCol == (int)i);
                if (ImGui::Selectable(state->parsedCsv.GetColumnName(i).c_str(), selected))
                    xCol = (int)i;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::PushID("ycol");
        if (ImGui::BeginCombo("Y Axis", state->parsedCsv.GetColumnName(yCol).c_str())) {
            for (size_t i = 0; i < colCount; ++i) {
                bool selected = (yCol == (int)i);
                if (ImGui::Selectable(state->parsedCsv.GetColumnName(i).c_str(), selected))
                    yCol = (int)i;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();

        // Prepare data
        static std::vector<double> xData, yData;
        xData.resize(plotRows);
        yData.resize(plotRows);
        bool validData = true;
        for (size_t i = 0; i < plotRows; ++i) {
            try {
                xData[i] = std::stod(state->parsedCsv.GetCell<std::string>(xCol, i));
                yData[i] = std::stod(state->parsedCsv.GetCell<std::string>(yCol, i));
            } catch (...) {
                validData = false;
                break;
            }
        }

        ImGui::Separator();
        if (validData) {
            if (ImPlot::BeginPlot("CSV Data Plot")) {
                ImPlot::PlotLine("Data", xData.data(), yData.data(), (int)plotRows);
                ImPlot::EndPlot();
            }
        } else {
            ImGui::TextColored(ImVec4(1,0,0,1), "Non-numeric data in selected columns.");
        }
        ImGui::End();
    }
    // Rendering
    ImGui::Render();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device); // Acquire a GPU command buffer

    SDL_GPUTexture* swapchain_texture;
    SDL_AcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr); // Acquire a swapchain texture

    if (swapchain_texture != nullptr && !is_minimized)
    {
        // This is mandatory: call Imgui_ImplSDLGPU3_PrepareDrawData() to upload the vertex/index buffer!
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

        // Setup and start a render pass
        SDL_GPUColorTargetInfo target_info = {};
        target_info.texture = swapchain_texture;
        target_info.clear_color = SDL_FColor{ 0.45, 0.45, 0.45, 1.0 };
        target_info.load_op = SDL_GPU_LOADOP_CLEAR;
        target_info.store_op = SDL_GPU_STOREOP_STORE;
        target_info.mip_level = 0;
        target_info.layer_or_depth_plane = 0;
        target_info.cycle = false;
        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);

        // Render ImGui
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);

        SDL_EndGPURenderPass(render_pass);
    }

    // Submit the command buffer
    SDL_SubmitGPUCommandBuffer(command_buffer);

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    // Cleanup
    SDL_WaitForGPUIdle(gpu_device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();
}