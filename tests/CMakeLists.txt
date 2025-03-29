# Find GTest package
find_package(GTest REQUIRED)

# Add test executable
add_executable(anarchy_tests
    test_main.cpp
    network_test.cpp
    vulkan_test.cpp
    protocol_test.cpp
    dx_compat_test.cpp
)

target_include_directories(anarchy_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/common
)

target_link_libraries(anarchy_tests
    PRIVATE
        anarchy_common
        GTest::GTest
)

# Enable testing
enable_testing()
add_test(NAME anarchy_tests COMMAND anarchy_tests) 