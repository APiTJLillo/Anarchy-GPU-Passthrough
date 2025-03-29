#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

namespace anarchy {
namespace gpu {

class VulkanUtils {
public:
    VulkanUtils();
    ~VulkanUtils();

    // Vulkan instance wrapper
    class Instance {
    public:
        explicit Instance(const std::vector<const char*>& extensions = {});
        ~Instance();

        vk::Instance get() const { return instance_.get(); }
        vk::PhysicalDevice getPhysicalDevice() const { return physical_device_; }

    private:
        std::vector<const char*> enabled_extensions_;
        vk::UniqueInstance instance_;
        vk::PhysicalDevice physical_device_;
    };

    // Vulkan device wrapper
    class Device {
    public:
        Device(const Instance& instance, const std::vector<const char*>& extensions = {});
        ~Device();

        vk::Device get() const { return device_.get(); }
        vk::PhysicalDevice physical_device_;

        // Command buffer management
        vk::CommandBuffer beginCommandBuffer();
        void endCommandBuffer(vk::CommandBuffer cmd_buffer);
        void submitCommandBuffer(vk::CommandBuffer cmd_buffer);

        // Frame capture
        std::vector<uint8_t> captureFramebuffer(vk::Image image, vk::Format format, 
            uint32_t width, uint32_t height);

    private:
        std::vector<const char*> enabled_extensions_;
        vk::UniqueDevice device_;
        vk::Queue graphics_queue_;
        vk::UniqueCommandPool command_pool_;
    };

    // Swapchain wrapper
    class Swapchain {
    public:
        Swapchain(const Device& device, vk::SurfaceKHR surface, 
            uint32_t width, uint32_t height);
        ~Swapchain();

        uint32_t acquireNextImage(vk::Semaphore semaphore);
        void present(uint32_t image_index, vk::Semaphore semaphore);

        vk::Format getFormat() const { return format_; }
        const std::vector<vk::Image>& getImages() const { return images_; }
        const std::vector<vk::ImageView>& getImageViews() const { return image_views_; }

    private:
        vk::Device device_;
        vk::Format format_;
        vk::UniqueSwapchainKHR swapchain_;
        std::vector<vk::Image> images_;
        std::vector<vk::ImageView> image_views_;
    };

    // Utility functions
    static std::vector<const char*> getRequiredExtensions();
    static bool checkDeviceExtensionSupport(vk::PhysicalDevice device, 
        const std::vector<const char*>& extensions);
    static vk::Format findSupportedFormat(vk::PhysicalDevice device,
        const std::vector<vk::Format>& candidates,
        vk::ImageTiling tiling,
        vk::FormatFeatureFlags features);
    static vk::Format findDepthFormat(vk::PhysicalDevice device);
    static uint32_t findMemoryType(vk::PhysicalDevice device,
        uint32_t type_filter,
        vk::MemoryPropertyFlags properties);

    static vk::UniqueBuffer createBuffer(vk::Device device, vk::PhysicalDevice physical_device,
        vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
    static vk::UniqueImageView createImageView(vk::Device device, vk::Image image,
        vk::Format format, vk::ImageAspectFlags aspectFlags);

private:
    // Helper functions
    static vk::UniqueCommandPool createCommandPool(vk::Device device, uint32_t queue_family);
    static vk::UniqueCommandBuffer createCommandBuffer(vk::Device device, vk::CommandPool pool);
    static void copyBuffer(vk::Device device, vk::CommandPool command_pool,
        vk::Queue queue, vk::Buffer src, vk::Buffer dst, vk::DeviceSize size);
};

} // namespace gpu
} // namespace anarchy 