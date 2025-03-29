#include "gpu/vulkan_utils.hpp"
#include <stdexcept>
#include <set>
#include <algorithm>

namespace anarchy {
namespace gpu {

VulkanUtils::VulkanUtils() {}

VulkanUtils::~VulkanUtils() {}

// Instance implementation
VulkanUtils::Instance::Instance(const std::vector<const char*>& extensions)
    : enabled_extensions_(extensions)
{
    // Get required extensions
    auto required_extensions = VulkanUtils::getRequiredExtensions();
    enabled_extensions_.insert(enabled_extensions_.end(),
        required_extensions.begin(), required_extensions.end());

    // Create instance
    vk::ApplicationInfo app_info(
        "Anarchy GPU Passthrough",
        VK_MAKE_VERSION(1, 0, 0),
        "Anarchy",
        VK_MAKE_VERSION(1, 0, 0),
        VK_API_VERSION_1_0
    );

    vk::InstanceCreateInfo create_info(
        vk::InstanceCreateFlags(),
        &app_info,
        0,  // enabled layer count
        nullptr,  // enabled layer names
        static_cast<uint32_t>(enabled_extensions_.size()),
        enabled_extensions_.data()
    );

    instance_ = vk::createInstanceUnique(create_info);

    // Select physical device
    auto devices = instance_->enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    // Select the first device for now (can be extended to select based on capabilities)
    physical_device_ = devices[0];
}

VulkanUtils::Instance::~Instance() = default;

// Device implementation
VulkanUtils::Device::Device(const Instance& instance, const std::vector<const char*>& extensions)
    : physical_device_(instance.getPhysicalDevice())
    , enabled_extensions_(extensions)
{
    // Find queue family
    auto queue_families = physical_device_.getQueueFamilyProperties();
    uint32_t graphics_queue_family = UINT32_MAX;

    for (uint32_t i = 0; i < queue_families.size(); i++) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphics_queue_family = i;
            break;
        }
    }

    if (graphics_queue_family == UINT32_MAX) {
        throw std::runtime_error("Failed to find graphics queue family!");
    }

    // Create device
    float queue_priority = 1.0f;
    vk::DeviceQueueCreateInfo queue_create_info(
        vk::DeviceQueueCreateFlags(),
        graphics_queue_family,
        1,  // queue count
        &queue_priority
    );

    vk::DeviceCreateInfo device_create_info(
        vk::DeviceCreateFlags(),
        1,  // queue create info count
        &queue_create_info,
        0,  // enabled layer count
        nullptr,  // enabled layer names
        static_cast<uint32_t>(enabled_extensions_.size()),
        enabled_extensions_.data()
    );

    device_ = physical_device_.createDeviceUnique(device_create_info);
    graphics_queue_ = device_->getQueue(graphics_queue_family, 0);
    command_pool_ = createCommandPool(device_.get(), graphics_queue_family);
}

VulkanUtils::Device::~Device() = default;

vk::CommandBuffer VulkanUtils::Device::beginCommandBuffer() {
    vk::CommandBufferAllocateInfo alloc_info(
        command_pool_.get(),
        vk::CommandBufferLevel::ePrimary,
        1
    );

    auto cmd_buffers = device_->allocateCommandBuffers(alloc_info);
    auto cmd_buffer = cmd_buffers[0];

    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    );

    cmd_buffer.begin(begin_info);
    return cmd_buffer;
}

void VulkanUtils::Device::endCommandBuffer(vk::CommandBuffer cmd_buffer) {
    cmd_buffer.end();
}

void VulkanUtils::Device::submitCommandBuffer(vk::CommandBuffer cmd_buffer) {
    vk::SubmitInfo submit_info(
        0,  // wait semaphore count
        nullptr,  // wait semaphores
        nullptr,  // wait dst stage mask
        1,  // command buffer count
        &cmd_buffer,
        0,  // signal semaphore count
        nullptr  // signal semaphores
    );

    graphics_queue_.submit(submit_info);
    graphics_queue_.waitIdle();
}

std::vector<uint8_t> VulkanUtils::Device::captureFramebuffer(
    vk::Image image, vk::Format format, uint32_t width, uint32_t height)
{
    // Create staging buffer
    vk::DeviceSize image_size = width * height * 4;  // RGBA
    auto staging_buffer = createBuffer(device_.get(), physical_device_,
        image_size, vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    // Create command buffer
    auto cmd_buffer = beginCommandBuffer();

    // Transition image layout
    vk::ImageMemoryBarrier barrier(
        vk::AccessFlagBits::eMemoryRead,
        vk::AccessFlagBits::eTransferRead,
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageLayout::eTransferSrcOptimal,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image,
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    );

    cmd_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlags(),
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // Copy image to buffer
    vk::BufferImageCopy region(
        0, 0, 0,
        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
        vk::Offset3D(0, 0, 0),
        vk::Extent3D(width, height, 1)
    );

    cmd_buffer.copyImageToBuffer(
        image, vk::ImageLayout::eTransferSrcOptimal,
        staging_buffer.get(), region
    );

    // Transition image back
    barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;

    cmd_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlags(),
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    endCommandBuffer(cmd_buffer);
    submitCommandBuffer(cmd_buffer);

    // Map buffer and copy data
    auto memory_reqs = device_->getBufferMemoryRequirements(staging_buffer.get());
    auto device_memory = device_->allocateMemory(vk::MemoryAllocateInfo(memory_reqs.size,
        findMemoryType(physical_device_, memory_reqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
    
    device_->bindBufferMemory(staging_buffer.get(), device_memory, 0);
    
    void* mapped = device_->mapMemory(device_memory, 0, memory_reqs.size);
    std::vector<uint8_t> pixels(static_cast<uint8_t*>(mapped),
        static_cast<uint8_t*>(mapped) + image_size);
    
    device_->unmapMemory(device_memory);
    device_->freeMemory(device_memory);
    
    return pixels;
}

// Swapchain implementation
VulkanUtils::Swapchain::Swapchain(const Device& device, vk::SurfaceKHR surface,
    uint32_t width, uint32_t height)
    : device_(device.get())
{
    // Get surface capabilities
    auto capabilities = device.physical_device_.getSurfaceCapabilitiesKHR(surface);
    
    // Choose surface format
    auto formats = device.physical_device_.getSurfaceFormatsKHR(surface);
    format_ = formats[0].format;
    
    // Choose present mode
    auto present_modes = device.physical_device_.getSurfacePresentModesKHR(surface);
    vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;

    // Create swapchain
    vk::SwapchainCreateInfoKHR create_info(
        vk::SwapchainCreateFlagsKHR(),
        surface,
        capabilities.minImageCount + 1,
        format_,
        vk::ColorSpaceKHR::eSrgbNonlinear,
        vk::Extent2D(width, height),
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        0,
        nullptr,
        vk::SurfaceTransformFlagBitsKHR::eIdentity,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        present_mode,
        VK_TRUE,
        nullptr
    );

    swapchain_ = device_.createSwapchainKHRUnique(create_info);
    images_ = device_.getSwapchainImagesKHR(swapchain_.get());
    
    // Create image views
    image_views_.reserve(images_.size());
    for (const auto& image : images_) {
        auto view = createImageView(device_, image, format_,
            vk::ImageAspectFlagBits::eColor);
        image_views_.push_back(view.get());
    }
}

VulkanUtils::Swapchain::~Swapchain() = default;

uint32_t VulkanUtils::Swapchain::acquireNextImage(vk::Semaphore semaphore) {
    auto result = device_.acquireNextImageKHR(swapchain_.get(), UINT64_MAX,
        semaphore, vk::Fence());
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }
    return result.value;
}

void VulkanUtils::Swapchain::present(uint32_t image_index, vk::Semaphore semaphore) {
    vk::PresentInfoKHR present_info(
        1, &semaphore,
        1, &swapchain_.get(),
        &image_index
    );

    if (device_.getQueue(0, 0).presentKHR(present_info) != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present swap chain image!");
    }
}

// Utility functions
std::vector<const char*> VulkanUtils::getRequiredExtensions() {
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    
    #ifdef VK_USE_PLATFORM_WAYLAND_KHR
    extensions.push_back("VK_KHR_wayland_surface");
    #endif
    
    #ifdef VK_USE_PLATFORM_XCB_KHR
    extensions.push_back("VK_KHR_xcb_surface");
    #endif
    
    #ifdef VK_USE_PLATFORM_XLIB_KHR
    extensions.push_back("VK_KHR_xlib_surface");
    #endif
    
    return extensions;
}

bool VulkanUtils::checkDeviceExtensionSupport(vk::PhysicalDevice device,
    const std::vector<const char*>& extensions)
{
    auto available_extensions = device.enumerateDeviceExtensionProperties();
    std::set<std::string> required_extensions(extensions.begin(), extensions.end());

    for (const auto& extension : available_extensions) {
        required_extensions.erase(extension.extensionName);
    }

    return required_extensions.empty();
}

vk::Format VulkanUtils::findSupportedFormat(vk::PhysicalDevice device,
    const std::vector<vk::Format>& candidates,
    vk::ImageTiling tiling,
    vk::FormatFeatureFlags features)
{
    for (vk::Format format : candidates) {
        auto props = device.getFormatProperties(format);
        if (tiling == vk::ImageTiling::eLinear &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal &&
            (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported format!");
}

vk::Format VulkanUtils::findDepthFormat(vk::PhysicalDevice device) {
    return findSupportedFormat(device,
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

uint32_t VulkanUtils::findMemoryType(vk::PhysicalDevice device,
    uint32_t type_filter,
    vk::MemoryPropertyFlags properties)
{
    auto mem_properties = device.getMemoryProperties();
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

// Helper functions
vk::UniqueCommandPool VulkanUtils::createCommandPool(vk::Device device, uint32_t queue_family) {
    vk::CommandPoolCreateInfo pool_info(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        queue_family
    );
    return device.createCommandPoolUnique(pool_info);
}

vk::UniqueCommandBuffer VulkanUtils::createCommandBuffer(vk::Device device, vk::CommandPool pool) {
    vk::CommandBufferAllocateInfo alloc_info(
        pool,
        vk::CommandBufferLevel::ePrimary,
        1
    );
    return std::move(device.allocateCommandBuffersUnique(alloc_info)[0]);
}

vk::UniqueBuffer VulkanUtils::createBuffer(vk::Device device, vk::PhysicalDevice physical_device,
    vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
{
    vk::BufferCreateInfo buffer_info(
        vk::BufferCreateFlags(),
        size,
        usage,
        vk::SharingMode::eExclusive,
        0,
        nullptr
    );

    auto buffer = device.createBufferUnique(buffer_info);
    auto mem_requirements = device.getBufferMemoryRequirements(buffer.get());
    
    vk::MemoryAllocateInfo alloc_info(
        mem_requirements.size,
        findMemoryType(physical_device, mem_requirements.memoryTypeBits, properties)
    );

    auto memory = device.allocateMemoryUnique(alloc_info);
    device.bindBufferMemory(buffer.get(), memory.get(), 0);

    return buffer;
}

void VulkanUtils::copyBuffer(vk::Device device, vk::CommandPool command_pool,
    vk::Queue queue, vk::Buffer src, vk::Buffer dst, vk::DeviceSize size)
{
    auto cmd_buffer = createCommandBuffer(device, command_pool);
    
    vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmd_buffer->begin(begin_info);

    vk::BufferCopy copy_region(0, 0, size);
    cmd_buffer->copyBuffer(src, dst, copy_region);

    cmd_buffer->end();

    vk::SubmitInfo submit_info(0, nullptr, nullptr, 1, &cmd_buffer.get());
    queue.submit(submit_info);
    queue.waitIdle();
}

vk::UniqueImageView VulkanUtils::createImageView(vk::Device device, vk::Image image,
    vk::Format format, vk::ImageAspectFlags aspect_flags)
{
    vk::ImageViewCreateInfo view_info(
        vk::ImageViewCreateFlags(),
        image,
        vk::ImageViewType::e2D,
        format,
        vk::ComponentMapping(),
        vk::ImageSubresourceRange(aspect_flags, 0, 1, 0, 1)
    );

    return device.createImageViewUnique(view_info);
}

} // namespace gpu
} // namespace anarchy 