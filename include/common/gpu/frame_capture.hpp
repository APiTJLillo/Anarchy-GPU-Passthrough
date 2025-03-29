#pragma once

#include <vulkan/vulkan.hpp>
#include <cuda_runtime.h>
#include <nvEncodeAPI.h>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>

namespace anarchy {
namespace gpu {

class FrameCapture {
public:
    struct CaptureConfig {
        uint32_t width;
        uint32_t height;
        VkFormat format;
        uint32_t fps;
        uint32_t bitrate;
        uint32_t gop_size;
        bool h264;
        bool hardware_encoding;
    };

    FrameCapture(const CaptureConfig& config);
    ~FrameCapture();

    // Initialize capture system
    bool initialize(VkDevice device, VkPhysicalDevice physical_device);
    
    // Capture and encode frame
    bool captureFrame(VkCommandBuffer command_buffer, VkImage image);
    
    // Get encoded frame data
    bool getEncodedFrame(std::vector<uint8_t>& frame_data);
    
    // Flush any pending frames
    void flush();
    
    // Get capture statistics
    struct Statistics {
        uint64_t frames_captured;
        uint64_t frames_encoded;
        uint64_t total_bytes;
        double average_fps;
        double average_latency;
    };
    Statistics getStatistics() const;

private:
    // CUDA resources
    struct CUDAResources {
        CUcontext context;
        CUstream stream;
        CUdeviceptr device_buffer;
        size_t buffer_size;
    };

    // NVENC resources
    struct NVENCResources {
        NV_ENCODE_API_FUNCTION_LIST nv_enc;
        void* encoder;
        NV_ENC_INITIALIZE_PARAMS init_params;
        NV_ENC_CONFIG encode_config;
        std::vector<NV_ENC_OUTPUT_PTR> output_buffers;
        std::vector<NV_ENC_REGISTERED_PTR> registered_buffers;
    };

    // Vulkan resources
    struct VulkanResources {
        VkDevice device;
        VkPhysicalDevice physical_device;
        VkBuffer staging_buffer;
        VkDeviceMemory staging_memory;
        VkCommandPool command_pool;
        VkCommandBuffer command_buffer;
    };

    // Configuration
    CaptureConfig config_;
    
    // Resources
    CUDAResources cuda_;
    NVENCResources nvenc_;
    VulkanResources vulkan_;
    
    // Frame queue
    struct FrameData {
        std::vector<uint8_t> data;
        uint64_t timestamp;
    };
    std::queue<FrameData> frame_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Encoding thread
    std::thread encode_thread_;
    bool should_stop_;
    
    // Statistics
    Statistics stats_;
    mutable std::mutex stats_mutex_;

    // Helper functions
    bool initializeCUDA();
    bool initializeNVENC();
    bool initializeVulkan();
    void cleanupResources();
    void encodeThread();
    bool copyImageToBuffer(VkCommandBuffer command_buffer, VkImage image);
    bool encodeFrame(const void* frame_data);
    void updateStatistics(uint64_t bytes, double latency);
};

} // namespace gpu
} // namespace anarchy 