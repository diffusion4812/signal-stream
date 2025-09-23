set(LIBS_SRCS
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_draw.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_widgets.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_tables.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/backends/imgui_impl_sdl3.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/backends/imgui_impl_sdlgpu3.cpp

    C:/Users/LOAR02/Source/Libraries/implot-master/implot.cpp
    C:/Users/LOAR02/Source/Libraries/implot-master/implot_items.cpp

    C:/Users/LOAR02/Source/Libraries/imgui_memory_editor.h

    C:/Users/LOAR02/Source/Libraries/rapidcsv.h

    C:/Users/LOAR02/Source/Libraries/json.hpp
)

source_group("Libraries" FILES ${LIBS_SRCS})
target_sources(signal-stream PRIVATE ${LIBS_SRCS})