#include <gtest/gtest.h>
#include "client/vulkan_icd.hpp"
#include "common/network/protocol.hpp"
#include <thread>
#include <chrono>

using namespace anarchy::client;
using namespace anarchy::network;

class VulkanICDTest : public ::testing::Test {
protected:
    void SetUp() override {
        icd = std::make_unique<VulkanICD>("tcp://127.0.0.1:5555");
    }

    void TearDown() override {
        icd.reset();
    }

    std::unique_ptr<VulkanICD> icd;
};

TEST_F(VulkanICDTest, InstanceCreation) {
    // Create instance create info
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = nullptr;
    create_info.enabledLayerCount = 0;
    create_info.ppEnabledLayerNames = nullptr;
    create_info.enabledExtensionCount = 0;
    create_info.ppEnabledExtensionNames = nullptr;

    // Create instance
    VkInstance instance;
    VkResult result = icd->vkCreateInstance(&create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_NE(instance, nullptr);

    // Enumerate physical devices
    uint32_t device_count = 0;
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_GT(device_count, 0);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    ASSERT_EQ(result, VK_SUCCESS);

    // Destroy instance
    icd->vkDestroyInstance(instance, nullptr);
}

TEST_F(VulkanICDTest, DeviceCreation) {
    // Create instance
    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkInstance instance;
    VkResult result = icd->vkCreateInstance(&instance_create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    // Get physical device
    uint32_t device_count = 0;
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_GT(device_count, 0);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    ASSERT_EQ(result, VK_SUCCESS);

    // Create device
    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = 0;
    queue_create_info.queueCount = 1;
    float queue_priority = 1.0f;
    queue_create_info.pQueuePriorities = &queue_priority;
    device_create_info.pQueueCreateInfos = &queue_create_info;

    VkDevice device;
    result = icd->vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_NE(device, nullptr);

    // Destroy device and instance
    icd->vkDestroyDevice(device, nullptr);
    icd->vkDestroyInstance(instance, nullptr);
}

TEST_F(VulkanICDTest, CommandBufferOperations) {
    // Create instance and device
    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkInstance instance;
    VkResult result = icd->vkCreateInstance(&instance_create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    uint32_t device_count = 0;
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_GT(device_count, 0);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    ASSERT_EQ(result, VK_SUCCESS);

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = 0;
    queue_create_info.queueCount = 1;
    float queue_priority = 1.0f;
    queue_create_info.pQueuePriorities = &queue_priority;
    device_create_info.pQueueCreateInfos = &queue_create_info;

    VkDevice device;
    result = icd->vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS);

    // Create command pool
    VkCommandPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.queueFamilyIndex = 0;
    VkCommandPool command_pool;
    result = icd->vkCreateCommandPool(device, &pool_create_info, nullptr, &command_pool);
    ASSERT_EQ(result, VK_SUCCESS);

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = command_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    result = icd->vkAllocateCommandBuffers(device, &allocate_info, &command_buffer);
    ASSERT_EQ(result, VK_SUCCESS);

    // Begin command buffer
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = icd->vkBeginCommandBuffer(command_buffer, &begin_info);
    ASSERT_EQ(result, VK_SUCCESS);

    // End command buffer
    result = icd->vkEndCommandBuffer(command_buffer);
    ASSERT_EQ(result, VK_SUCCESS);

    // Free command buffer
    icd->vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

    // Destroy command pool
    icd->vkDestroyCommandPool(device, command_pool, nullptr);

    // Destroy device and instance
    icd->vkDestroyDevice(device, nullptr);
    icd->vkDestroyInstance(instance, nullptr);
}

TEST_F(VulkanICDTest, SwapchainOperations) {
    // Create instance and device
    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkInstance instance;
    VkResult result = icd->vkCreateInstance(&instance_create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    uint32_t device_count = 0;
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_GT(device_count, 0);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    ASSERT_EQ(result, VK_SUCCESS);

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = 0;
    queue_create_info.queueCount = 1;
    float queue_priority = 1.0f;
    queue_create_info.pQueuePriorities = &queue_priority;
    device_create_info.pQueueCreateInfos = &queue_create_info;

    VkDevice device;
    result = icd->vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS);

    // Create swapchain
    VkSwapchainCreateInfoKHR swapchain_create_info = {};
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = nullptr;  // Mock surface
    swapchain_create_info.minImageCount = 2;
    swapchain_create_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swapchain_create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_create_info.imageExtent = {800, 600};
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain;
    result = icd->vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain);
    ASSERT_EQ(result, VK_SUCCESS);

    // Acquire next image
    uint32_t image_index;
    result = icd->vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
        VK_NULL_HANDLE, VK_NULL_HANDLE, &image_index);
    ASSERT_EQ(result, VK_SUCCESS);

    // Present image
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;

    result = icd->vkQueuePresentKHR(VK_NULL_HANDLE, &present_info);
    ASSERT_EQ(result, VK_SUCCESS);

    // Destroy swapchain
    icd->vkDestroySwapchainKHR(device, swapchain, nullptr);

    // Destroy device and instance
    icd->vkDestroyDevice(device, nullptr);
    icd->vkDestroyInstance(instance, nullptr);
}

TEST_F(VulkanICDTest, MemoryOperations) {
    // Create instance and device
    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkInstance instance;
    VkResult result = icd->vkCreateInstance(&instance_create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    uint32_t device_count = 0;
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_GT(device_count, 0);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    ASSERT_EQ(result, VK_SUCCESS);

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = 0;
    queue_create_info.queueCount = 1;
    float queue_priority = 1.0f;
    queue_create_info.pQueuePriorities = &queue_priority;
    device_create_info.pQueueCreateInfos = &queue_create_info;

    VkDevice device;
    result = icd->vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS);

    // Allocate memory
    VkMemoryAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = 1024;
    allocate_info.memoryTypeIndex = 0;

    VkDeviceMemory memory;
    result = icd->vkAllocateMemory(device, &allocate_info, nullptr, &memory);
    ASSERT_EQ(result, VK_SUCCESS);

    // Map memory
    void* data;
    result = icd->vkMapMemory(device, memory, 0, 1024, 0, &data);
    ASSERT_EQ(result, VK_SUCCESS);

    // Unmap memory
    icd->vkUnmapMemory(device, memory);

    // Free memory
    icd->vkFreeMemory(device, memory, nullptr);

    // Destroy device and instance
    icd->vkDestroyDevice(device, nullptr);
    icd->vkDestroyInstance(instance, nullptr);
}

TEST_F(VulkanICDTest, Synchronization) {
    // Create instance and device
    VkInstanceCreateInfo instance_create_info = {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkInstance instance;
    VkResult result = icd->vkCreateInstance(&instance_create_info, nullptr, &instance);
    ASSERT_EQ(result, VK_SUCCESS);

    uint32_t device_count = 0;
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    ASSERT_EQ(result, VK_SUCCESS);
    ASSERT_GT(device_count, 0);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = icd->vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    ASSERT_EQ(result, VK_SUCCESS);

    VkDeviceCreateInfo device_create_info = {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = 0;
    queue_create_info.queueCount = 1;
    float queue_priority = 1.0f;
    queue_create_info.pQueuePriorities = &queue_priority;
    device_create_info.pQueueCreateInfos = &queue_create_info;

    VkDevice device;
    result = icd->vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device);
    ASSERT_EQ(result, VK_SUCCESS);

    // Create semaphore
    VkSemaphoreCreateInfo semaphore_create_info = {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore;
    result = icd->vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore);
    ASSERT_EQ(result, VK_SUCCESS);

    // Create fence
    VkFenceCreateInfo fence_create_info = {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    result = icd->vkCreateFence(device, &fence_create_info, nullptr, &fence);
    ASSERT_EQ(result, VK_SUCCESS);

    // Wait for fence
    result = icd->vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    ASSERT_EQ(result, VK_SUCCESS);

    // Reset fence
    result = icd->vkResetFences(device, 1, &fence);
    ASSERT_EQ(result, VK_SUCCESS);

    // Destroy fence and semaphore
    icd->vkDestroyFence(device, fence, nullptr);
    icd->vkDestroySemaphore(device, semaphore, nullptr);

    // Destroy device and instance
    icd->vkDestroyDevice(device, nullptr);
    icd->vkDestroyInstance(instance, nullptr);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 