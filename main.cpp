#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <implot.h>

#include <iostream>

#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_timer.h>

#include "rapidcsv.h"

/* We will use this renderer to draw into this window every frame. */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_GPUDevice *gpu_device = NULL;

static SDL_FPoint points[500];

static ImGuiIO io;

typedef struct {
    double fps;
    double *fpshistory;
    Uint64 frameCount;
    Uint64 lastTime;
    double frequency;

    bool fileIsRead;
    SDL_IOStream *fileStream;
    size_t fileSize;
    size_t bytesRead;
    char *fileBuffer;
} AppState;

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    AppState *state = (AppState *)SDL_malloc(sizeof(AppState));
    if (!state) {
        SDL_Log("Failed to allocate memory for app state: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    *appstate = state;
    state->fps = 0.0;
    state->lastTime = SDL_GetPerformanceCounter();
    state->frequency = (double)SDL_GetPerformanceFrequency();
    state->frameCount = 0;
    state->fpshistory = (double *)SDL_malloc(5000 * sizeof(double));
    if (!state->fpshistory) {
        SDL_Log("Failed to allocate memory for FPS history: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    for (int i = 0; i < 5000; i++) {
        state->fpshistory[i] = 0.0;
    }

    state->fileIsRead = false;
    state->fileStream = NULL;
    state->fileSize = 0;
    state->bytesRead = 0;
    state->fileBuffer = NULL;

    // Setup SDL
    if (!SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        return SDL_APP_FAILURE;
    }

    // Create SDL window graphics context
    window = SDL_CreateWindow("Dear ImGui SDL3+SDL_GPU example", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create GPU Device
    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB, true, nullptr);
    if (gpu_device == nullptr)
    {
        SDL_Log("Failed to create GPU device: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Claim window for GPU Device
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window))
    {
        SDL_Log("Failed to claim window for GPU device: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create set GPU swapchain parameters
    if (!SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC))
    {
        SDL_Log("Failed to set GPU swapchain parameters: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    ImPlot::CreateContext();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = gpu_device;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&init_info);

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }
    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *state = (AppState *)appstate;
    io = ImGui::GetIO();

    if (!state->fileStream) { // Open a file for reading
        state->fileStream = SDL_IOFromFile("example.csv", "r");
        if (!state->fileStream) {
            SDL_Log("Failed to open file: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }
    if (!state->fileBuffer) { // Allocate a buffer for reading
        // Note: In a real application, you would probably want to allocate a buffer based on the file size or use a dynamic buffer.
        state->fileBuffer = (char *)SDL_malloc(2048); // Allocate a buffer for reading
        if (!state->fileBuffer) {
            SDL_Log("Failed to allocate memory: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
    }
    if (SDL_GetIOStatus(state->fileStream) != SDL_IO_STATUS_EOF) {
        state->bytesRead += SDL_ReadIO(state->fileStream, state->fileBuffer + state->bytesRead , sizeof(state->fileBuffer));
    }
    else {
        state->fileIsRead = true; // File is fully read
    }

    if (state->fileIsRead) {
        std::stringstream sstream(state->fileBuffer);
        rapidcsv::Document doc(sstream, rapidcsv::LabelParams(0, -1), rapidcsv::SeparatorParams(',', '"', '\\'));
        SDL_Log("CSV Document loaded with %zu rows and %zu columns.", doc.GetRowCount(), doc.GetColumnCount());
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

    ImGui::Begin("FPS Display", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImPlot::BeginPlot("FPS");
    ImPlot::PlotBars("FPS", state->fpshistory, 5000);
    ImPlot::EndPlot();
    ImGui::End();

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device); // Acquire a GPU command buffer

    SDL_GPUTexture* swapchain_texture;
    SDL_AcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr, nullptr); // Acquire a swapchain texture
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    if (swapchain_texture != nullptr)
    {
        // This is mandatory: call Imgui_ImplSDLGPU3_PrepareDrawData() to upload the vertex/index buffer!
        Imgui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

        // Setup and start a render pass
        SDL_GPUColorTargetInfo target_info = {};
        target_info.texture = swapchain_texture;
        target_info.clear_color = SDL_FColor { clear_color.x, clear_color.y, clear_color.z, clear_color.w };
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

    Uint64 currentTime = SDL_GetPerformanceCounter();
    double deltaTime = (double)(currentTime - state->lastTime) / state->frequency;
    state->lastTime = currentTime;

    state->fps = 1.0 / deltaTime;

    state->fpshistory[state->frameCount % 5000] = state->fps; // Store the FPS in the history
    state->frameCount++;

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    AppState *state = (AppState *)appstate;

    /* SDL will clean up the window/renderer for us. */
    SDL_CloseIO(state->fileStream); // Close the IO stream
    state->fileStream = NULL;
    SDL_free(state->fileBuffer); // Free the buffer
    state->fileBuffer = NULL;
    ImGui_ImplSDLGPU3_Shutdown();
}