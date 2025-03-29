#include "server/frame_capture.hpp"

namespace anarchy {
namespace server {

FrameCapture::FrameCapture(gpu::VulkanUtils::Device& device)
    : device_(device)
{
}

FrameCapture::~FrameCapture() = default;

std::vector<uint8_t> FrameCapture::captureFrame(uint32_t width, uint32_t height) {
    // TODO: Implement frame capture
    return std::vector<uint8_t>();
}

} // namespace server
} // namespace anarchy 