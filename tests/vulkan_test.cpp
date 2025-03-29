#include <gtest/gtest.h>
#include "common/gpu/vulkan_utils.hpp"

using namespace anarchy::gpu;

class VulkanTest : public ::testing::Test {
protected:
    void SetUp() override {
        instance = std::make_unique<VulkanUtils::Instance>();
        device = std::make_unique<VulkanUtils::Device>(*instance);
    }

    void TearDown() override {
        device.reset();
        instance.reset();
    }

    std::unique_ptr<VulkanUtils::Instance> instance;
    std::unique_ptr<VulkanUtils::Device> device;
};

TEST_F(VulkanTest, InstanceCreation) {
    ASSERT_TRUE(instance->get());
    ASSERT_TRUE(instance->getPhysicalDevice());
}

TEST_F(VulkanTest, DeviceCreation) {
    ASSERT_TRUE(device->get());
}

TEST_F(VulkanTest, CommandBufferCreation) {
    auto cmd_buffer = device->beginCommandBuffer();
    ASSERT_TRUE(cmd_buffer);
    device->endCommandBuffer(cmd_buffer);
}

TEST_F(VulkanTest, BufferCreation) {
    vk::DeviceSize buffer_size = 1024;
    auto buffer = VulkanUtils::createBuffer(
        device->get(),
        device->physical_device_,
        buffer_size,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    ASSERT_TRUE(buffer.operator bool());
}

TEST_F(VulkanTest, ImageViewCreation) {
    vk::ImageCreateInfo image_info(
        vk::ImageCreateFlags(),
        vk::ImageType::e2D,
        vk::Format::eR8G8B8A8Unorm,
        vk::Extent3D(800, 600, 1),
        1, 1,
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment
    );

    auto image = device->get().createImageUnique(image_info);
    ASSERT_TRUE(image.operator bool());

    auto image_view = VulkanUtils::createImageView(
        device->get(),
        image.get(),
        vk::Format::eR8G8B8A8Unorm,
        vk::ImageAspectFlagBits::eColor
    );
    ASSERT_TRUE(image_view.operator bool());
}

TEST_F(VulkanTest, FormatSupport) {
    auto physical_device = instance->getPhysicalDevice();
    
    // Test finding supported format
    auto format = VulkanUtils::findSupportedFormat(
        physical_device,
        {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eColorAttachment
    );

    ASSERT_NE(format, vk::Format::eUndefined);

    // Test finding depth format
    auto depth_format = VulkanUtils::findDepthFormat(physical_device);
    ASSERT_NE(depth_format, vk::Format::eUndefined);
}

TEST_F(VulkanTest, MemoryTypeSelection) {
    auto physical_device = instance->getPhysicalDevice();
    
    // Test finding memory type
    uint32_t memory_type = VulkanUtils::findMemoryType(
        physical_device,
        0xFFFFFFFF,  // All memory types
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );

    ASSERT_NE(memory_type, UINT32_MAX);
}

TEST_F(VulkanTest, DeviceExtensionSupport) {
    auto physical_device = instance->getPhysicalDevice();
    std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE1_EXTENSION_NAME
    };

    bool supported = VulkanUtils::checkDeviceExtensionSupport(physical_device, extensions);
    ASSERT_TRUE(supported);
}

TEST_F(VulkanTest, BasicTest) {
    EXPECT_TRUE(true);
} 