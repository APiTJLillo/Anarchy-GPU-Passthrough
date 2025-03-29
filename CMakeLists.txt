cmake_minimum_required(VERSION 3.10)
project(Anarchy-GPU-Passthrough)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)
pkg_check_modules(LZ4 REQUIRED liblz4)
find_package(ZLIB REQUIRED)
find_package(Vulkan REQUIRED)
find_package(GTest REQUIRED)
find_package(CUDA REQUIRED)
find_package(OpenSSL REQUIRED)

# Set include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/common
    ${CMAKE_SOURCE_DIR}/include/server
    ${Vulkan_INCLUDE_DIRS}
    ${ZeroMQ_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Common library
add_library(anarchy_common
    src/common/network/zmq_wrapper.cpp
)

target_include_directories(anarchy_common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(anarchy_common
    PUBLIC
        ${ZeroMQ_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${LZ4_LIBRARIES}
        Vulkan::Vulkan
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Server executable
add_executable(anarchy_server
    src/server/main.cpp
)

target_include_directories(anarchy_server
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server
)

target_link_libraries(anarchy_server
    PRIVATE
        anarchy_common
)

# Tests
add_executable(anarchy_tests
    tests/test_main.cpp
    tests/network_test.cpp
    tests/protocol_test.cpp
    tests/vulkan_test.cpp
    tests/dx_compat_test.cpp
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
        GTest::Main
)

# Platform-specific settings
if(WIN32)
    find_package(DirectX REQUIRED)
    add_definitions(-DHAS_DIRECTX)
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Documentation
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif() 