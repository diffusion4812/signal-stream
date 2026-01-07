set(LIBS_SRCS
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_draw.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_widgets.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/imgui_tables.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/backends/imgui_impl_sdl3.cpp
    C:/Users/LOAR02/Source/Libraries/imgui-docking/backends/imgui_impl_sdlgpu3.cpp

    C:/Users/LOAR02/Source/Libraries/implot/implot.cpp
    C:/Users/LOAR02/Source/Libraries/implot/implot_items.cpp

    C:/Users/LOAR02/Source/Libraries/imgui_memory_editor.h

    C:/Users/LOAR02/Source/Libraries/imgui-filebrowser/imfilebrowser.h

    C:/Users/LOAR02/Source/Libraries/rapidcsv/src/rapidcsv.h

    C:/Users/LOAR02/Source/Libraries/json.hpp
)

source_group("Libraries" FILES ${LIBS_SRCS})
target_sources(signal-stream PRIVATE ${LIBS_SRCS})

add_subdirectory(C:/Users/LOAR02/Source/Libraries/spdlog spdlog)
target_link_libraries(signal-stream spdlog)

target_link_libraries(signal-stream
    $<$<CONFIG:Debug>:arrow.lib>
    $<$<CONFIG:Debug>:parquet.lib>
    $<$<CONFIG:Release>:arrow.lib>
    $<$<CONFIG:Release>:parquet.lib>
)

add_subdirectory(C:/Users/LOAR02/Source/symbol-resolver SymbolResolver)
target_link_libraries(signal-stream SymbolResolver)