cmake_minimum_required(VERSION 3.13)
project(signal-stream LANGUAGES CXX)

include_directories(C:/Users/LOAR02/Source/Libraries/SDL3-x64/include)
include_directories(C:/Users/LOAR02/Source/Libraries/imgui-docking)
include_directories(C:/Users/LOAR02/Source/Libraries/imgui-docking/backends)
include_directories(C:/Users/LOAR02/Source/Libraries/implot-master)
include_directories(C:/Users/LOAR02/Source/Libraries/tinycc/win32/libtcc)
include_directories(C:/Users/LOAR02/Source/Libraries)
include_directories($ENV{BOOST_ROOT})

include_directories(include)
include_directories(include/window)
include_directories(libs)

link_directories(C:/Users/LOAR02/Source/Libraries/SDL3-x64/lib)

add_executable(signal-stream WIN32)
set_property(TARGET signal-stream PROPERTY CXX_STANDARD 20)
set_property(TARGET signal-stream PROPERTY OUTPUT_NAME "Signal Stream")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

target_link_libraries(signal-stream SDL3)

add_subdirectory(libs)
add_subdirectory(misc)
add_subdirectory(include)
add_subdirectory(source)

enable_testing()
add_subdirectory(tests)