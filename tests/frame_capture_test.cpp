#include <gtest/gtest.h>
#include "common/gpu/frame_capture.hpp"
#include "common/gpu/vulkan_utils.hpp"
#include <thread>
#include <chrono>

using namespace anarchy::gpu;

class FrameCaptureTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create Vulkan instance
        VkInstanceCreateInfo instance_create_info = {};
        instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &instance_);
        ASSERT_EQ(result, VK_SUCCESS);

        // Get physical device
        uint32_t device_count = 0;
        result = vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
        ASSERT_EQ(result, VK_SUCCESS);
        ASSERT_GT(device_count, 0);

        std::vector<VkPhysicalDevice> physical_devices(device_count);
        result = vkEnumeratePhysicalDevices(instance_, &device_count, physical_devices.data());
        ASSERT_EQ(result, VK_SUCCESS);

        // Create logical device
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

        result = vkCreateDevice(physical_devices[0], &device_create_info, nullptr, &device_);
        ASSERT_EQ(result, VK_SUCCESS);

        // Create frame capture
        FrameCapture::CaptureConfig config;
        config.width = 1920;
        config.height = 1080;
        config.format = VK_FORMAT_B8G8R8A8_UNORM;
        config.fps = 60;
        config.bitrate = 5000000; // 5 Mbps
        config.gop_size = 30;
        config.h264 = true;
        config.hardware_encoding = true;

        frame_capture_ = std::make_unique<FrameCapture>(config);
        ASSERT_TRUE(frame_capture_->initialize(device_, physical_devices[0]));
    }

    void TearDown() override {
        frame_capture_.reset();
        vkDestroyDevice(device_, nullptr);
        vkDestroyInstance(instance_, nullptr);
    }

    VkInstance instance_;
    VkDevice device_;
    std::unique_ptr<FrameCapture> frame_capture_;
};

TEST_F(FrameCaptureTest, FrameCaptureAndEncode) {
    // Create test image
    VkImageCreateInfo image_create_info = {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_create_info.extent.width = 1920;
    image_create_info.extent.height = 1080;
    image_create_info.extent.depth = 1;
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    VkResult result = vkCreateImage(device_, &image_create_info, nullptr, &image);
    ASSERT_EQ(result, VK_SUCCESS);

    // Create command buffer
    VkCommandPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.queueFamilyIndex = 0;

    VkCommandPool command_pool;
    result = vkCreateCommandPool(device_, &pool_create_info, nullptr, &command_pool);
    ASSERT_EQ(result, VK_SUCCESS);

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    result = vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer);
    ASSERT_EQ(result, VK_SUCCESS);

    // Capture frame
    ASSERT_TRUE(frame_capture_->captureFrame(command_buffer, image));

    // Get encoded frame
    std::vector<uint8_t> frame_data;
    ASSERT_TRUE(frame_capture_->getEncodedFrame(frame_data));
    ASSERT_FALSE(frame_data.empty());

    // Clean up
    vkFreeCommandBuffers(device_, command_pool, 1, &command_buffer);
    vkDestroyCommandPool(device_, command_pool, nullptr);
    vkDestroyImage(device_, image, nullptr);
}

TEST_F(FrameCaptureTest, Statistics) {
    // Create test image and command buffer (similar to previous test)
    // ... (reuse code from previous test)

    // Capture multiple frames
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(frame_capture_->captureFrame(command_buffer, image));
        std::vector<uint8_t> frame_data;
        ASSERT_TRUE(frame_capture_->getEncodedFrame(frame_data));
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    // Check statistics
    auto stats = frame_capture_->getStatistics();
    ASSERT_GT(stats.frames_captured, 0);
    ASSERT_GT(stats.frames_encoded, 0);
    ASSERT_GT(stats.total_bytes, 0);
    ASSERT_GT(stats.average_fps, 0);
    ASSERT_GE(stats.average_latency, 0);

    // Clean up
    // ... (reuse cleanup code from previous test)
}

TEST_F(FrameCaptureTest, Flush) {
    // Create test image and command buffer (similar to previous test)
    // ... (reuse code from previous test)

    // Capture multiple frames
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(frame_capture_->captureFrame(command_buffer, image));
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Flush and verify queue is empty
    frame_capture_->flush();
    std::vector<uint8_t> frame_data;
    ASSERT_FALSE(frame_capture_->getEncodedFrame(frame_data));

    // Clean up
    // ... (reuse cleanup code from previous test)
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 