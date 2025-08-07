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
} AppState;

static Console* console = nullptr;

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
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

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

    /*if (!state->fileStream) { // Open a file for reading
        state->fileStream = SDL_IOFromFile("C:/Users/LOAR02/Source/sdl3-playground/build/Debug/example.csv", "r");
        if (!state->fileStream) {
            SDL_Log("Failed to open file: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }
    if (!state->fileBuffer) { // Allocate a buffer for reading
        // Note: In a real application, you would probably want to allocate a buffer based on the file size or use a dynamic buffer.
        state->fileBuffer = (char*)SDL_malloc(2048); // Allocate a buffer for reading
        if (!state->fileBuffer) {
            SDL_Log("Failed to allocate memory: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }
    if (SDL_GetIOStatus(state->fileStream) != SDL_IO_STATUS_EOF) {
        state->bytesRead += SDL_ReadIO(state->fileStream, state->fileBuffer + state->bytesRead, sizeof(state->fileBuffer));
    }
    else {
        state->fileIsRead = true; // File is fully read
    }

    if (state->fileIsRead) {
        std::stringstream sstream(state->fileBuffer);
        rapidcsv::Document doc(sstream, rapidcsv::LabelParams(0, -1), rapidcsv::SeparatorParams(',', '"', '\\'));
        SDL_Log("CSV Document loaded with %zu rows and %zu columns.", doc.GetRowCount(), doc.GetColumnCount());
    }*/

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

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

    {
        ImGui::Begin("Console");
        if (ImGui::Button("Clear Console")) {
            console->removeAll();
        }
        ImGui::SameLine();
        ImGui::Text("Log Count: %d", console->getCount());
        ImGui::Text("Console Output:");
        std::string consoleOutput;
        console->getItemsAsString(consoleOutput);
        ImGui::TextUnformatted(consoleOutput.c_str());
        ImGui::End();
    }

    if (state->filePath != "") {
        ImGui::Begin("CSV Plot", nullptr);
        ImGui::Text("File Path: %s", state->filePath.c_str());
        ImGui::End();
    }

    // Rendering
    ImGui::Render();
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