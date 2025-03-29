#include <gtest/gtest.h>
#include "server/gpu_server.hpp"
#include "common/network/protocol.hpp"
#include <thread>
#include <chrono>

using namespace anarchy::server;
using namespace anarchy::network;

class GPUServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<GPUServer>("tcp://127.0.0.1:5555");
        ASSERT_TRUE(server->start());
    }

    void TearDown() override {
        server->stop();
        server.reset();
    }

    std::unique_ptr<GPUServer> server;
};

TEST_F(GPUServerTest, ServerLifecycle) {
    ASSERT_TRUE(server->isRunning());
    server->stop();
    ASSERT_FALSE(server->isRunning());
}

TEST_F(GPUServerTest, ConnectionHandling) {
    // Create a client message
    Message connect_msg;
    connect_msg.header.type = MessageType::CONNECT;
    connect_msg.header.size = 0;
    connect_msg.header.sequence = 1;

    // Process connection message
    server->processCommand(connect_msg);

    // Create a heartbeat message
    Message heartbeat_msg;
    heartbeat_msg.header.type = MessageType::HEARTBEAT;
    heartbeat_msg.header.size = 0;
    heartbeat_msg.header.sequence = 2;

    // Process heartbeat message
    server->processCommand(heartbeat_msg);

    // Create a disconnect message
    Message disconnect_msg;
    disconnect_msg.header.type = MessageType::DISCONNECT;
    disconnect_msg.header.size = 0;
    disconnect_msg.header.sequence = 3;

    // Process disconnect message
    server->processCommand(disconnect_msg);
}

TEST_F(GPUServerTest, VulkanCommandHandling) {
    // Test create instance
    Message create_instance_msg;
    create_instance_msg.header.type = MessageType::VK_CREATE_INSTANCE;
    create_instance_msg.header.size = 0;
    create_instance_msg.header.sequence = 1;
    server->processCommand(create_instance_msg);

    // Test create device
    Message create_device_msg;
    create_device_msg.header.type = MessageType::VK_CREATE_DEVICE;
    create_device_msg.header.size = 0;
    create_device_msg.header.sequence = 2;
    server->processCommand(create_device_msg);

    // Test create command pool
    Message create_pool_msg;
    create_pool_msg.header.type = MessageType::VK_CREATE_COMMAND_POOL;
    create_pool_msg.header.size = 0;
    create_pool_msg.header.sequence = 3;
    server->processCommand(create_pool_msg);

    // Test create command buffer
    Message create_buffer_msg;
    create_buffer_msg.header.type = MessageType::VK_CREATE_COMMAND_BUFFER;
    create_buffer_msg.header.size = 0;
    create_buffer_msg.header.sequence = 4;
    server->processCommand(create_buffer_msg);

    // Test begin command buffer
    Message begin_buffer_msg;
    begin_buffer_msg.header.type = MessageType::VK_BEGIN_COMMAND_BUFFER;
    begin_buffer_msg.header.size = 0;
    begin_buffer_msg.header.sequence = 5;
    server->processCommand(begin_buffer_msg);

    // Test end command buffer
    Message end_buffer_msg;
    end_buffer_msg.header.type = MessageType::VK_END_COMMAND_BUFFER;
    end_buffer_msg.header.size = 0;
    end_buffer_msg.header.sequence = 6;
    server->processCommand(end_buffer_msg);

    // Test queue submit
    Message submit_msg;
    submit_msg.header.type = MessageType::VK_QUEUE_SUBMIT;
    submit_msg.header.size = 0;
    submit_msg.header.sequence = 7;
    server->processCommand(submit_msg);
}

TEST_F(GPUServerTest, SwapchainOperations) {
    // Test create swapchain
    struct SwapchainCreateParams {
        uint32_t width = 800;
        uint32_t height = 600;
        vk::SurfaceKHR surface = nullptr;  // Mock surface
    };

    Message create_swapchain_msg;
    create_swapchain_msg.header.type = MessageType::VK_CREATE_SWAPCHAIN;
    create_swapchain_msg.header.size = sizeof(SwapchainCreateParams);
    create_swapchain_msg.header.sequence = 1;
    create_swapchain_msg.payload.resize(sizeof(SwapchainCreateParams));
    std::memcpy(create_swapchain_msg.payload.data(), &SwapchainCreateParams{}, sizeof(SwapchainCreateParams));
    server->processCommand(create_swapchain_msg);

    // Test acquire next image
    Message acquire_msg;
    acquire_msg.header.type = MessageType::VK_ACQUIRE_NEXT_IMAGE;
    acquire_msg.header.size = 0;
    acquire_msg.header.sequence = 2;
    server->processCommand(acquire_msg);

    // Test present
    struct PresentParams {
        uint32_t image_index = 0;
        vk::Semaphore semaphore = nullptr;  // Mock semaphore
    };

    Message present_msg;
    present_msg.header.type = MessageType::VK_PRESENT;
    present_msg.header.size = sizeof(PresentParams);
    present_msg.header.sequence = 3;
    present_msg.payload.resize(sizeof(PresentParams));
    std::memcpy(present_msg.payload.data(), &PresentParams{}, sizeof(PresentParams));
    server->processCommand(present_msg);
}

TEST_F(GPUServerTest, FrameCapture) {
    // Set up frame state
    struct FrameState {
        vk::Image image = nullptr;  // Mock image
        vk::Format format = vk::Format::eR8G8B8A8Unorm;
        uint32_t width = 800;
        uint32_t height = 600;
    };

    Message frame_request_msg;
    frame_request_msg.header.type = MessageType::FRAME_REQUEST;
    frame_request_msg.header.size = 0;
    frame_request_msg.header.sequence = 1;
    server->processCommand(frame_request_msg);
}

TEST_F(GPUServerTest, ErrorHandling) {
    // Test invalid command type
    Message invalid_msg;
    invalid_msg.header.type = static_cast<MessageType>(999);  // Invalid type
    invalid_msg.header.size = 0;
    invalid_msg.header.sequence = 1;
    server->processCommand(invalid_msg);

    // Test invalid sequence number
    Message invalid_seq_msg;
    invalid_seq_msg.header.type = MessageType::VK_CREATE_COMMAND_BUFFER;
    invalid_seq_msg.header.size = 0;
    invalid_seq_msg.header.sequence = 999;  // Invalid sequence
    server->processCommand(invalid_seq_msg);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 