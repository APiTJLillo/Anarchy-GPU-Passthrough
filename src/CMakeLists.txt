# Common library
add_library(anarchy_common
    common/network/zmq_wrapper.cpp
)

target_include_directories(anarchy_common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/common
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
    server/main.cpp
)

target_include_directories(anarchy_server
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server
)

target_link_libraries(anarchy_server
    PRIVATE
        anarchy_common
)

# Client library (Windows DLL)
if(WIN32)
    add_library(anarchy_client SHARED
        client/main.cpp
        client/vulkan_icd.cpp
        client/frame_decoder.cpp
    )

    target_include_directories(anarchy_client
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include/client
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/client
    )

    target_link_libraries(anarchy_client
        PRIVATE
            anarchy_common
    )

    # Set output name for Windows DLL
    set_target_properties(anarchy_client PROPERTIES
        OUTPUT_NAME "anarchy_client"
        PREFIX ""
    )
endif() 