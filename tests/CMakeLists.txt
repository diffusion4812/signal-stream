## ============================================================================
## Google Test Setup
## ============================================================================
##add_subdirectory(C:/Users/LOAR02/Source/Libraries/googletest 
##                 googletest-build 
##                 EXCLUDE_FROM_ALL)
#
## Google Test
#include(FetchContent)
#FetchContent_Declare(googletest
#    GIT_REPOSITORY https://github.com/google/googletest.git
#    GIT_TAG v1.14.0
#)
##set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
#FetchContent_MakeAvailable(googletest)
#
## ============================================================================
## Compiler Settings
## ============================================================================
## Set UTF-8 encoding for MSVC compilers
#if(MSVC)
#    add_compile_options(/utf-8)
#endif()
#
## Set C++20 standard globally for test targets
#set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)
#
## ============================================================================
## Test Executables
## ============================================================================
## Project Test
#add_executable(project_test
#    ../source/sourceregistration.cpp
#    ../source/schema.cpp
#    ../source/service-storage.cpp
#    ../source/service-source-registry.cpp
#    ../source/service-project.cpp
#    test-project.cpp
#)
#
## Backend Test
#add_executable(backend_test
#    ../source/service-storage.cpp
#    ../source/schema.cpp
#    test-backend.cpp
#)
#
## Storage Test
#add_executable(storage_test
#    ../source/service-storage.cpp
#    ../source/schema.cpp
#    test-storage.cpp
#)
#
## Expression Test
#add_executable(expression_test
#    test-expression.cpp
#)
#
## ============================================================================
## Compiler-Specific Options
## ============================================================================
#if(MSVC)
#    target_compile_options(expression_test PRIVATE /bigobj)
#endif()
#
## ============================================================================
## Link Libraries
## ============================================================================
## Link Google Test to all test executables
#target_link_libraries(project_test PRIVATE gmock_main)
#target_link_libraries(expression_test PRIVATE gmock_main)
#
## Backend and Storage tests require Arrow and Parquet
#target_link_libraries(backend_test PRIVATE
#    gmock_main
#    $<$<CONFIG:Debug>:arrow>
#    $<$<CONFIG:Debug>:parquet>
#    $<$<CONFIG:Release>:arrow>
#    $<$<CONFIG:Release>:parquet>
#)
#target_link_libraries(storage_test PRIVATE
#    gmock_main
#    $<$<CONFIG:Debug>:arrow>
#    $<$<CONFIG:Debug>:parquet>
#    $<$<CONFIG:Release>:arrow>
#    $<$<CONFIG:Release>:parquet>
#)
#
## ============================================================================
## Post-Build: Copy Runtime Dependencies
## ============================================================================
#if(DEFINED ARROW_DEBUG_ROOT AND DEFINED ARROW_RELEASE_ROOT)
#    # Function to copy Arrow/Parquet DLLs to target directory
#    function(copy_arrow_dlls TARGET_NAME)
#        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
#            COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                "$<$<CONFIG:Debug>:${ARROW_DEBUG_ROOT}/bin/arrow.dll>$<$<CONFIG:Release>:${ARROW_RELEASE_ROOT}/bin/arrow.dll>"
#                $<TARGET_FILE_DIR:${TARGET_NAME}>
#            COMMENT "Copying Arrow DLL to ${TARGET_NAME} for $<CONFIG>"
#        )
#
#        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
#            COMMAND ${CMAKE_COMMAND} -E copy_if_different
#                "$<$<CONFIG:Debug>:${ARROW_DEBUG_ROOT}/bin/parquet.dll>$<$<CONFIG:Release>:${ARROW_RELEASE_ROOT}/bin/parquet.dll>"
#                $<TARGET_FILE_DIR:${TARGET_NAME}>
#            COMMENT "Copying Parquet DLL to ${TARGET_NAME} for $<CONFIG>"
#        )
#    endfunction()
#
#    # Apply DLL copy to both tests
#    copy_arrow_dlls(backend_test)
#    copy_arrow_dlls(storage_test)
#endif()
#
## ============================================================================
## Test Discovery
## ============================================================================
#include(GoogleTest)
#gtest_discover_tests(project_test)
#gtest_discover_tests(backend_test)
#gtest_discover_tests(storage_test)
#gtest_discover_tests(expression_test)