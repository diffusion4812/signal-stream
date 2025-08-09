cmake_minimum_required(VERSION 3.10)
project(sdl-playground CXX)

include_directories(C:/Users/LOAR02/Source/Libraries/SDL3-x64/include)
include_directories(C:/Users/LOAR02/Source/Libraries/imgui-docking)
include_directories(C:/Users/LOAR02/Source/Libraries/imgui-docking/backends)
include_directories(C:/Users/LOAR02/Source/Libraries/implot-master)

link_directories(C:/Users/LOAR02/Source/Libraries/SDL3-x64/lib)

add_executable(sdl-playground WIN32
    main.cpp
    console.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_draw.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_widgets.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_tables.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/backends/imgui_impl_sdl3.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/backends/imgui_impl_sdlgpu3.cpp
    C:/Users/LOAR02/Source/Libraries/implot-master/implot.cpp
    C:/Users/LOAR02/Source/Libraries/implot-master/implot_items.cpp)

target_link_libraries(sdl-playground SDL3)

set_property(TARGET sdl-playground PROPERTY CXX_STANDARD 17)