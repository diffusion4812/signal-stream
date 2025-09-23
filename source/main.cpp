#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <implot.h>

#include <iostream>
#include <string>
#include <filesystem>
#include <deque>
#include <vector>
#include <memory>
#include <chrono>

#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_mutex.h>

#include "rapidcsv.h"
#include "libtcc.h"

#include "appstate.h"
#include "console.h"
#include "window.h"
#include "csv.h"
#include "tcpip.h"
 
static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_GPUDevice* gpu_device = NULL;
static ImGuiIO io;

static const uint64_t idleThresholdNS = 3000000000;
static const uint64_t minFrameTimeNS = 2000000; // 2ms minimum frame time to avoid busy-waiting
static const double activeFPS = 60.0;
static const double idleFPS = 5.0;

static int SDLCALL IOThread(void* userdata) {
    boost::asio::io_context* io_context = static_cast<boost::asio::io_context*>(userdata);
    io_context->run();
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

    while (*filelist) {
        prepAndReadFile(userdata, *filelist);
        filelist++;
    }
}

static void SDLCALL appSDL_LogOutputFunction(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    AppState* state = (AppState*)userdata;
    state->console->log((int)priority, std::string(message));
}

inline void NotifyUserInput(void* userdata)
{
    AppState* state = (AppState*)userdata;
    state->lastInputTimestamp = SDL_GetTicksNS();
    state->isIdle = false;
}

void IdleMode_HandleFrameThrottling(void* userdata) {
    AppState* state = (AppState*)userdata;
    auto targetFPS = state->isIdle ? idleFPS : activeFPS;
    uint64_t targetFrameTimeNS = 1000000000 / targetFPS;

    auto now = SDL_GetTicksNS();
    auto idleDur = now - state->lastInputTimestamp;
    if (!state->isIdle && idleDur >= idleThresholdNS) {
        state->isIdle = true;
        SDL_Log("Entering idle mode (no user input for %lld ms)", idleDur / 1000000);
    }

    auto elapsedNS = now - state->lastTime;

    if (elapsedNS < targetFrameTimeNS) {
        uint64_t sleepNS = targetFrameTimeNS - elapsedNS;
        if (sleepNS > minFrameTimeNS) {
            SDL_DelayPrecise(sleepNS);
        }
    }

    now = SDL_GetTicksNS();
    elapsedNS = now - state->lastTime;
    state->fps = (elapsedNS > 0) ? 1000000000.0 / elapsedNS : 0.0;

    state->lastTime = SDL_GetTicksNS();
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    AppState* state = new AppState();
    if (!state) {
        SDL_Log("Failed to allocate memory for app state: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    *appstate = state;

    state->csvFiles.reserve(10);
    state->console = new Console();
    SDL_SetLogOutputFunction(appSDL_LogOutputFunction, state);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) != 0)
    {
        SDL_Log("Error: SDL_Init(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create SDL window graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    window = SDL_CreateWindow("Signal Stream", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
    {
        SDL_Log("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // ... (load your icon image into an SDL_Surface)
    SDL_Surface* icon_surface = SDL_LoadBMP("icon.bmp");
    if (icon_surface) {
        SDL_SetWindowIcon(window, icon_surface);
        SDL_DestroySurface(icon_surface); // Free the surface after setting the icon
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
#ifdef USE_PLATFORM_WINDOWS
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Viewports
#endif
    io.IniFilename = NULL; // Disable ini file saving

    // Setup Dear ImGui style
    if (SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK) {
        ImGui::StyleColorsDark();
    }
    else {
        ImGui::StyleColorsLight();
    }

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

    state->fps = 0.0;

    state->windows.push_back(std::make_unique<Window_FPS>(&state->fps));

    state->io_context = new boost::asio::io_context();
    state->work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(state->io_context->get_executor());
    state->ioThread = SDL_CreateThread(IOThread, "IOThread", state->io_context);

    state->buffer = new SPSC_CircularBuffer<std::byte*>(1024);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    switch (event->type) {
    case SDL_EVENT_DROP_FILE:
        SDL_Log("File dropped: %s", event->drop.data);
        prepAndReadFile(appstate, event->drop.data);
        return SDL_APP_CONTINUE;
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        if (SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK) {
            ImGui::StyleColorsDark();
        } else {
            ImGui::StyleColorsLight();
        }
        return SDL_APP_CONTINUE;
    case SDL_EVENT_QUIT:
        SDL_Log("Received quit event.");
        return SDL_APP_SUCCESS;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOVED:
        NotifyUserInput(appstate);
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

    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File")) {
        if(ImGui::BeginMenu("New Project")) {
            if (ImGui::MenuItem("TCP/IP")) {
                state->schema = new Schema();
                state->schema->add_field("timestamp", Kind::Int64);
                state->schema->add_field("value", Kind::Double);
                state->schema->finalize();

                state->servers.push_back(std::make_unique<Server>(*state->io_context, 26201, state->schema, state->buffer));
                state->windows.push_back(std::make_unique<Window_Live>(state->buffer));
            }
            if (ImGui::MenuItem("Open Serial")) {
                // Placeholder for future Serial functionality
                SDL_Log("Serial functionality not implemented yet.");
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open CSV")) {
            SDL_ShowOpenFileDialog(callback, appstate, nullptr, nullptr, 0, nullptr, false);
        }
        if (ImGui::MenuItem("Exit")) {
            return SDL_APP_SUCCESS;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Open Console")) {
            state->windows.push_back(std::make_unique<Window_Console>(state->console, &state->consoleIsOpen));
        }
        if (ImGui::MenuItem("Open Metrics Window")) {
            state->show_metrics_window = true;
        }
        if (ImGui::MenuItem("Open Debug Log")) {
            state->show_debug_log = true;
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();

    for (auto& window : state->windows) {
        window->Draw();
    }

    if (state->show_metrics_window) {
        ImGui::ShowMetricsWindow(&state->show_metrics_window);
    }
    if (state->show_debug_log) {
        ImGui::ShowDebugLogWindow(&state->show_debug_log);
    }

    for (auto &file : state->csvFiles) {
        if (file.csvTableWindowIsOpen) {
            ImGui::Begin(file.filePath.filename().string().c_str(), &file.csvTableWindowIsOpen);
            ImGui::Text("File Path: %s", file.filePath.string().c_str());

            // Show a table with the first few rows of the CSV file
            if (file.fileIsRead) {
                size_t colCount = file.parsedCsv->GetColumnCount();
                size_t rowCount = file.parsedCsv->GetRowCount();
                size_t previewRows = (rowCount < 15) ? rowCount : 10;

                if (ImGui::BeginTable("CSVTable", static_cast<int>(colCount), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    // Header row
                    ImGui::TableNextRow();
                    for (size_t col = 0; col < colCount; ++col) {
                        ImGui::TableSetColumnIndex(static_cast<int>(col));
                        ImGui::TextUnformatted(file.parsedCsv->GetColumnName(col).c_str());
                    }
                    // Data rows
                    for (size_t row = 0; row < previewRows; ++row) {
                        ImGui::TableNextRow();
                        for (size_t col = 0; col < colCount; ++col) {
                            ImGui::TableSetColumnIndex(static_cast<int>(col));
                            try {
                                ImGui::TextUnformatted(file.parsedCsv->GetCell<std::string>(col, row).c_str());
                            }
                            catch (...) {
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

    }

    // Rendering
    ImGui::Render();
#ifdef USE_PLATFORM_WINDOWS
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
#endif
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

    IdleMode_HandleFrameThrottling(appstate);

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