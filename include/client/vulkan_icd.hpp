#pragma once

#include "common/network/zmq_wrapper.hpp"
#include "common/network/protocol.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>

namespace anarchy {
namespace client {

class VulkanICD {
public:
    VulkanICD(const std::string& server_address);
    ~VulkanICD();

    // Vulkan instance functions
    VkResult vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
    void vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator);
    VkResult vkEnumeratePhysicalDevices(VkInstance instance,
        uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);

    // Vulkan device functions
    VkResult vkCreateDevice(VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
        VkDevice* pDevice);
    void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);
    VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
        const char* pLayerName, uint32_t* pPropertyCount,
        VkExtensionProperties* pProperties);

    // Vulkan swapchain functions
    VkResult vkCreateSwapchainKHR(VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
    void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
        const VkAllocationCallbacks* pAllocator);
    VkResult vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
        uint64_t timeout, VkSemaphore semaphore, VkFence fence,
        uint32_t* pImageIndex);
    VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

    // Vulkan command buffer functions
    VkResult vkCreateCommandPool(VkDevice device,
        const VkCommandPoolCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool);
    void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
        const VkAllocationCallbacks* pAllocator);
    VkResult vkAllocateCommandBuffers(VkDevice device,
        const VkCommandBufferAllocateInfo* pAllocateInfo,
        VkCommandBuffer* pCommandBuffers);
    void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
        uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);
    VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
        const VkCommandBufferBeginInfo* pBeginInfo);
    VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer);
    VkResult vkResetCommandBuffer(VkCommandBuffer commandBuffer,
        VkCommandBufferResetFlags flags);

    // Vulkan queue functions
    VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount,
        const VkSubmitInfo* pSubmits, VkFence fence);
    VkResult vkQueueWaitIdle(VkQueue queue);

    // Vulkan memory functions
    VkResult vkAllocateMemory(VkDevice device,
        const VkMemoryAllocateInfo* pAllocateInfo,
        const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
    void vkFreeMemory(VkDevice device, VkDeviceMemory memory,
        const VkAllocationCallbacks* pAllocator);
    VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory,
        VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags,
        void** ppData);
    void vkUnmapMemory(VkDevice device, VkDeviceMemory memory);

    // Vulkan buffer functions
    VkResult vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer);
    void vkDestroyBuffer(VkDevice device, VkBuffer buffer,
        const VkAllocationCallbacks* pAllocator);
    VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer,
        VkDeviceMemory memory, VkDeviceSize memoryOffset);

    // Vulkan image functions
    VkResult vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkImage* pImage);
    void vkDestroyImage(VkDevice device, VkImage image,
        const VkAllocationCallbacks* pAllocator);
    VkResult vkBindImageMemory(VkDevice device, VkImage image,
        VkDeviceMemory memory, VkDeviceSize memoryOffset);

    // Vulkan synchronization functions
    VkResult vkCreateSemaphore(VkDevice device,
        const VkSemaphoreCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
    void vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
        const VkAllocationCallbacks* pAllocator);
    VkResult vkCreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkFence* pFence);
    void vkDestroyFence(VkDevice device, VkFence fence,
        const VkAllocationCallbacks* pAllocator);
    VkResult vkWaitForFences(VkDevice device, uint32_t fenceCount,
        const VkFence* pFences, VkBool32 waitAll, uint64_t timeout);
    VkResult vkResetFences(VkDevice device, uint32_t fenceCount,
        const VkFence* pFences);

private:
    // Network communication
    std::unique_ptr<network::ZMQWrapper> network_;
    std::string server_address_;

    // Resource tracking
    struct InstanceInfo {
        VkInstance instance;
        std::vector<VkPhysicalDevice> physical_devices;
    };
    struct DeviceInfo {
        VkDevice device;
        VkPhysicalDevice physical_device;
        std::vector<VkQueue> queues;
    };
    struct SwapchainInfo {
        VkSwapchainKHR swapchain;
        VkDevice device;
        std::vector<VkImage> images;
    };
    struct CommandPoolInfo {
        VkCommandPool command_pool;
        VkDevice device;
    };
    struct CommandBufferInfo {
        VkCommandBuffer command_buffer;
        VkCommandPool command_pool;
        VkDevice device;
    };

    std::unordered_map<VkInstance, InstanceInfo> instances_;
    std::unordered_map<VkDevice, DeviceInfo> devices_;
    std::unordered_map<VkSwapchainKHR, SwapchainInfo> swapchains_;
    std::unordered_map<VkCommandPool, CommandPoolInfo> command_pools_;
    std::unordered_map<VkCommandBuffer, CommandBufferInfo> command_buffers_;

    std::mutex instance_mutex_;
    std::mutex device_mutex_;
    std::mutex swapchain_mutex_;
    std::mutex command_pool_mutex_;
    std::mutex command_buffer_mutex_;

    // Helper functions
    VkResult sendCommand(const network::Message& message);
    VkResult waitForResponse(uint64_t sequence);
    void handleResponse(const network::Message& message);
    void handleError(const network::Message& message);
    void cleanupResources();
};

} // namespace client
} // namespace anarchy 