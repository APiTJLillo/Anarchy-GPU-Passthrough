#pragma once

#include "gpu/vulkan_utils.hpp"
#include <vector>
#include <cstdint>

namespace anarchy {
namespace server {

class FrameCapture {
public:
    FrameCapture(gpu::VulkanUtils::Device& device);
    ~FrameCapture();

    std::vector<uint8_t> captureFrame(uint32_t width, uint32_t height);

private:
    gpu::VulkanUtils::Device& device_;
};

} // namespace server
} // namespace anarchy 