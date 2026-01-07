cmake_minimum_required(VERSION 3.26)
project(signal-stream LANGUAGES CXX VERSION 1.0 DESCRIPTION "Signal Stream")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ARROW_DEBUG_ROOT   "C:/Users/LOAR02/Source/Libraries/arrow/install/debug")
set(ARROW_RELEASE_ROOT "C:/Users/LOAR02/Source/Libraries/arrow/install/release")

# Include directories
include_directories(
    C:/Users/LOAR02/Source/Libraries/SDL3-x64/include
    C:/Users/LOAR02/Source/Libraries/imgui-docking
    C:/Users/LOAR02/Source/Libraries/imgui-docking/backends
    C:/Users/LOAR02/Source/Libraries/imgui-filebrowser
    C:/Users/LOAR02/Source/Libraries/implot
    C:/Users/LOAR02/Source/Libraries/tinycc/win32/libtcc
    C:/Users/LOAR02/Source/Libraries/async_mqtt/include
    C:/Users/LOAR02/Source/Libraries/rapidcsv/src
    C:/Users/LOAR02/Source/Libraries/boost/build/include/boost-1_90
    C:/Users/LOAR02/Source/Libraries/spdlog/include
    C:/Users/LOAR02/Source/Libraries
    $ENV{BOOST_ROOT}
    include
    include/window
    include/service
    include/source
    libs
    ${ARROW_DEBUG_ROOT}/include
    ${ARROW_RELEASE_ROOT}/include
    "C:/Program Files/Microsoft Visual Studio/18/Community/DIA SDK/include"
)

# Link directories for .lib files
link_directories(
    C:/Users/LOAR02/Source/Libraries/SDL3-x64/lib
    $<$<CONFIG:Debug>:${ARROW_DEBUG_ROOT}/lib>
    $<$<CONFIG:Release>:${ARROW_RELEASE_ROOT}/lib>
)

add_executable(signal-stream WIN32)

set_property(TARGET signal-stream PROPERTY OUTPUT_NAME "Signal Stream")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")

# Windows version macro
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00) # Windows 10
endif()

# Link against import libs
target_link_libraries(signal-stream
    SDL3
    $<$<CONFIG:Debug>:arrow.lib>
    $<$<CONFIG:Debug>:parquet.lib>
    $<$<CONFIG:Release>:arrow.lib>
    $<$<CONFIG:Release>:parquet.lib>
)

# Copy correct DLLs after build
add_custom_command(TARGET signal-stream POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<$<CONFIG:Debug>:${ARROW_DEBUG_ROOT}/bin/arrow.dll>
        $<$<CONFIG:Release>:${ARROW_RELEASE_ROOT}/bin/arrow.dll>
        $<TARGET_FILE_DIR:signal-stream>
)
add_custom_command(TARGET signal-stream POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<$<CONFIG:Debug>:${ARROW_DEBUG_ROOT}/bin/parquet.dll>
        $<$<CONFIG:Release>:${ARROW_RELEASE_ROOT}/bin/parquet.dll>
        $<TARGET_FILE_DIR:signal-stream>
)

# Subdirectories
add_subdirectory(libs)
add_subdirectory(misc)
add_subdirectory(include)
add_subdirectory(source)
enable_testing()
add_subdirectory(tests)