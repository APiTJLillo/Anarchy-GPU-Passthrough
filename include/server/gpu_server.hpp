#pragma once

#include "common/network/zmq_wrapper.hpp"
#include "common/network/protocol.hpp"
#include "common/gpu/vulkan_utils.hpp"
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <string>
#include <condition_variable>

namespace anarchy {
namespace server {

class GPUServer {
public:
    GPUServer(const std::string& address);
    ~GPUServer();

    // Server lifecycle
    void start();
    void stop();
    bool isRunning() const;

    // Command processing
    void processCommand(const network::Message& message);
    void handleVulkanCommand(const network::Message& message);
    void handleFrameRequest(const network::Message& message);

    // Vulkan command handlers
    void handleCreateInstance(const network::Message& message);
    void handleCreateDevice(const network::Message& message);
    void handleCreateSwapchain(const network::Message& message);
    void handleCreateCommandPool(const network::Message& message);
    void handleCreateCommandBuffer(const network::Message& message);
    void handleBeginCommandBuffer(const network::Message& message);
    void handleEndCommandBuffer(const network::Message& message);
    void handleQueueSubmit(const network::Message& message);
    void handleAcquireNextImage(const network::Message& message);
    void handlePresent(const network::Message& message);

private:
    // Vulkan instance and device management
    std::unique_ptr<gpu::VulkanUtils::Instance> vulkan_instance_;
    std::unique_ptr<gpu::VulkanUtils::Device> vulkan_device_;
    std::unique_ptr<gpu::VulkanUtils::Swapchain> vulkan_swapchain_;

    // Network communication
    std::unique_ptr<network::ZMQWrapper> zmq_;
    std::string server_address_;

    // Command tracking
    struct CommandState {
        vk::CommandBuffer command_buffer;
        vk::CommandPool command_pool;
        vk::Queue queue;
    };
    std::unordered_map<uint64_t, CommandState> command_states_;
    std::mutex command_mutex_;

    // Frame capture
    struct FrameState {
        vk::Image image;
        vk::Format format;
        uint32_t width;
        uint32_t height;
    };
    std::unordered_map<uint64_t, FrameState> frame_states_;
    std::mutex frame_mutex_;

    // Server state
    bool running_;
    std::mutex state_mutex_;
    std::condition_variable state_cv_;

    // Helper functions
    void sendResponse(const network::Message& original_message, 
        const std::vector<uint8_t>& response_data);
    void sendError(const network::Message& original_message, 
        uint32_t error_code, const std::string& error_message);
    void handleConnection(const network::Message& message);
    void handleDisconnection(const network::Message& message);
    void handleHeartbeat(const network::Message& message);
};

} // namespace server
} // namespace anarchy 