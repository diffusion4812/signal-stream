add_subdirectory(C:/Users/LOAR02/Source/Libraries/googletest-1.17.0 googletest-build EXCLUDE_FROM_ALL)

add_executable(project_test
    ../source/serviceregistration.cpp
    ../source/schema.cpp
    project_test.cpp
)
add_executable(storage_test
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

link_directories(C:/Users/LOAR02/Source/Libraries/googletest-1.17.0)
target_link_libraries(project_test PRIVATE gtest_main)
target_link_libraries(storage_test PRIVATE gtest_main)
target_link_libraries(expression_test PRIVATE gtest_main)

include(GoogleTest)
gtest_discover_tests(project_test)
gtest_discover_tests(storage_test)
gtest_discover_tests(expression_test)