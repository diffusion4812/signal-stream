add_subdirectory(C:/Users/LOAR02/Source/Libraries/googletest googletest-build EXCLUDE_FROM_ALL)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_executable(project_test
    ../source/sourceregistration.cpp
    ../source/schema.cpp
    ../source/service-storage.cpp
    ../source/service-source-registry.cpp
    ../source/service-project.cpp
    project_test.cpp
)
add_executable(storage_test
    ../source/service-storage.cpp
    ../source/schema.cpp
    storage_test.cpp
)
add_executable(expression_test
    expression_test.cpp
)

set_property(TARGET project_test PROPERTY CXX_STANDARD 20)
set_property(TARGET storage_test PROPERTY CXX_STANDARD 20)
set_property(TARGET expression_test PROPERTY CXX_STANDARD 20)
if(MSVC)
    target_compile_options(expression_test PRIVATE /bigobj)
endif()

link_directories(C:/Users/LOAR02/Source/Libraries/googletest)

target_link_libraries(project_test PRIVATE
    gmock_main
)

target_link_libraries(storage_test PRIVATE
    gmock_main
    $<$<CONFIG:Debug>:arrow.lib>
    $<$<CONFIG:Debug>:parquet.lib>
    $<$<CONFIG:Release>:arrow.lib>
    $<$<CONFIG:Release>:parquet.lib>
)

target_link_libraries(expression_test PRIVATE
    gmock_main
)

# Copy correct DLLs after build
add_custom_command(TARGET storage_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<$<CONFIG:Debug>:${ARROW_DEBUG_ROOT}/bin/arrow.dll>
        $<$<CONFIG:Release>:${ARROW_RELEASE_ROOT}/bin/arrow.dll>
        $<TARGET_FILE_DIR:storage_test>
)
add_custom_command(TARGET storage_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<$<CONFIG:Debug>:${ARROW_DEBUG_ROOT}/bin/parquet.dll>
        $<$<CONFIG:Release>:${ARROW_RELEASE_ROOT}/bin/parquet.dll>
        $<TARGET_FILE_DIR:storage_test>
)

include(GoogleTest)
gtest_discover_tests(project_test)
gtest_discover_tests(storage_test)
gtest_discover_tests(expression_test)
