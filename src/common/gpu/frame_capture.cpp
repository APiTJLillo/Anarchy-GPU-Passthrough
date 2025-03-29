#include "common/gpu/frame_capture.hpp"
#include <stdexcept>
#include <cstring>

namespace anarchy {
namespace gpu {

FrameCapture::FrameCapture(const CaptureConfig& config)
    : config_(config)
    , should_stop_(false)
{
    // Initialize statistics
    stats_ = Statistics{};
}

FrameCapture::~FrameCapture() {
    flush();
    should_stop_ = true;
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }
    cleanupResources();
}

bool FrameCapture::initialize(VkDevice device, VkPhysicalDevice physical_device) {
    vulkan_.device = device;
    vulkan_.physical_device = physical_device;

    // Initialize subsystems
    if (!initializeVulkan()) {
        return false;
    }
    if (!initializeCUDA()) {
        return false;
    }
    if (!initializeNVENC()) {
        return false;
    }

    // Start encoding thread
    encode_thread_ = std::thread(&FrameCapture::encodeThread, this);
    return true;
}

bool FrameCapture::initializeVulkan() {
    // Create staging buffer
    VkBufferCreateInfo buffer_create_info = {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = config_.width * config_.height * 4; // RGBA
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(vulkan_.device, &buffer_create_info, nullptr, &vulkan_.staging_buffer);
    if (result != VK_SUCCESS) {
        return false;
    }

    // Get memory requirements
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(vulkan_.device, vulkan_.staging_buffer, &mem_requirements);

    // Allocate memory
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = 0; // TODO: Find suitable memory type

    result = vkAllocateMemory(vulkan_.device, &alloc_info, nullptr, &vulkan_.staging_memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(vulkan_.device, vulkan_.staging_buffer, nullptr);
        return false;
    }

    // Bind memory
    result = vkBindBufferMemory(vulkan_.device, vulkan_.staging_buffer, vulkan_.staging_memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(vulkan_.device, vulkan_.staging_memory, nullptr);
        vkDestroyBuffer(vulkan_.device, vulkan_.staging_buffer, nullptr);
        return false;
    }

    // Create command pool
    VkCommandPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create_info.queueFamilyIndex = 0; // TODO: Get from device

    result = vkCreateCommandPool(vulkan_.device, &pool_create_info, nullptr, &vulkan_.command_pool);
    if (result != VK_SUCCESS) {
        vkFreeMemory(vulkan_.device, vulkan_.staging_memory, nullptr);
        vkDestroyBuffer(vulkan_.device, vulkan_.staging_buffer, nullptr);
        return false;
    }

    // Create command buffer
    VkCommandBufferAllocateInfo alloc_info_cb = {};
    alloc_info_cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info_cb.commandPool = vulkan_.command_pool;
    alloc_info_cb.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info_cb.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(vulkan_.device, &alloc_info_cb, &vulkan_.command_buffer);
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(vulkan_.device, vulkan_.command_pool, nullptr);
        vkFreeMemory(vulkan_.device, vulkan_.staging_memory, nullptr);
        vkDestroyBuffer(vulkan_.device, vulkan_.staging_buffer, nullptr);
        return false;
    }

    return true;
}

bool FrameCapture::initializeCUDA() {
    // Initialize CUDA
    CUresult result = cuInit(0);
    if (result != CUDA_SUCCESS) {
        return false;
    }

    // Create CUDA context
    result = cuCtxCreate(&cuda_.context, 0, 0);
    if (result != CUDA_SUCCESS) {
        return false;
    }

    // Create CUDA stream
    result = cuStreamCreate(&cuda_.stream, 0);
    if (result != CUDA_SUCCESS) {
        cuCtxDestroy(cuda_.context);
        return false;
    }

    // Allocate device buffer
    cuda_.buffer_size = config_.width * config_.height * 4;
    result = cuMemAlloc(&cuda_.device_buffer, cuda_.buffer_size);
    if (result != CUDA_SUCCESS) {
        cuStreamDestroy(cuda_.stream);
        cuCtxDestroy(cuda_.context);
        return false;
    }

    return true;
}

bool FrameCapture::initializeNVENC() {
    // Load NVENC API
    NVENCSTATUS status = NvEncodeAPICreateInstance(&nvenc_.nv_enc);
    if (status != NV_ENC_SUCCESS) {
        return false;
    }

    // Initialize encoder
    NV_ENC_INITIALIZE_PARAMS init_params = {};
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init_params.encodeConfig = &nvenc_.encode_config;
    init_params.encodeConfig->version = NV_ENC_CONFIG_VER;
    init_params.encodeConfig->rcParams.version = NV_ENC_RC_PARAMS_VER;
    init_params.encodeConfig->encodeCodecConfig.h264Config.version = NV_ENC_CODEC_CONFIG_VER;

    // Configure encoding parameters
    init_params.encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
    init_params.encodeConfig->rcParams.averageBitRate = config_.bitrate;
    init_params.encodeConfig->rcParams.maxBitRate = config_.bitrate;
    init_params.encodeConfig->rcParams.vbvBufferSize = config_.bitrate / config_.fps;
    init_params.encodeConfig->rcParams.vbvInitialDelay = init_params.encodeConfig->rcParams.vbvBufferSize;
    init_params.encodeConfig->rcParams.maxQP = 51;
    init_params.encodeConfig->rcParams.minQP = 0;

    // Configure H.264 parameters
    init_params.encodeConfig->encodeCodecConfig.h264Config.idrPeriod = config_.gop_size;
    init_params.encodeConfig->encodeCodecConfig.h264Config.maxNumRefFramesInDPB = 4;

    // Create encoder
    status = nvenc_.nv_enc.nvEncInitializeEncoder(&nvenc_.encoder, &init_params);
    if (status != NV_ENC_SUCCESS) {
        return false;
    }

    // Allocate output buffers
    nvenc_.output_buffers.resize(4);
    for (auto& buffer : nvenc_.output_buffers) {
        NV_ENC_CREATE_BITSTREAM_BUFFER create_bitstream_buffer = {};
        create_bitstream_buffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        create_bitstream_buffer.size = config_.width * config_.height * 4;
        create_bitstream_buffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;

        status = nvenc_.nv_enc.nvEncCreateBitstreamBuffer(nvenc_.encoder, &create_bitstream_buffer);
        if (status != NV_ENC_SUCCESS) {
            return false;
        }
        buffer = create_bitstream_buffer.bitstreamBuffer;
    }

    return true;
}

void FrameCapture::cleanupResources() {
    // Clean up NVENC resources
    if (nvenc_.encoder) {
        nvenc_.nv_enc.nvEncDestroyEncoder(nvenc_.encoder);
    }

    // Clean up CUDA resources
    if (cuda_.device_buffer) {
        cuMemFree(cuda_.device_buffer);
    }
    if (cuda_.stream) {
        cuStreamDestroy(cuda_.stream);
    }
    if (cuda_.context) {
        cuCtxDestroy(cuda_.context);
    }

    // Clean up Vulkan resources
    if (vulkan_.command_buffer) {
        vkFreeCommandBuffers(vulkan_.device, vulkan_.command_pool, 1, &vulkan_.command_buffer);
    }
    if (vulkan_.command_pool) {
        vkDestroyCommandPool(vulkan_.device, vulkan_.command_pool, nullptr);
    }
    if (vulkan_.staging_memory) {
        vkFreeMemory(vulkan_.device, vulkan_.staging_memory, nullptr);
    }
    if (vulkan_.staging_buffer) {
        vkDestroyBuffer(vulkan_.device, vulkan_.staging_buffer, nullptr);
    }
}

bool FrameCapture::captureFrame(VkCommandBuffer command_buffer, VkImage image) {
    if (!copyImageToBuffer(command_buffer, image)) {
        return false;
    }

    // Copy from staging buffer to CUDA buffer
    void* staging_data;
    vkMapMemory(vulkan_.device, vulkan_.staging_memory, 0, cuda_.buffer_size, 0, &staging_data);
    
    CUresult result = cuMemcpyHtoDAsync(cuda_.device_buffer, staging_data, cuda_.buffer_size, cuda_.stream);
    vkUnmapMemory(vulkan_.device, vulkan_.staging_memory);
    
    if (result != CUDA_SUCCESS) {
        return false;
    }

    // Queue frame for encoding
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        FrameData frame_data;
        frame_data.data.resize(cuda_.buffer_size);
        frame_data.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        result = cuMemcpyDtoHAsync(frame_data.data.data(), cuda_.device_buffer, cuda_.buffer_size, cuda_.stream);
        if (result != CUDA_SUCCESS) {
            return false;
        }
        
        frame_queue_.push(std::move(frame_data));
        queue_cv_.notify_one();
    }

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.frames_captured++;
    }

    return true;
}

bool FrameCapture::getEncodedFrame(std::vector<uint8_t>& frame_data) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (frame_queue_.empty()) {
        return false;
    }

    FrameData frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    lock.unlock();

    // Encode frame
    if (!encodeFrame(frame.data.data())) {
        return false;
    }

    // Get encoded data from output buffer
    NV_ENC_LOCK_BITSTREAM lock_bitstream = {};
    lock_bitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
    lock_bitstream.outputBitstream = nvenc_.output_buffers[0];

    NVENCSTATUS status = nvenc_.nv_enc.nvEncLockBitstream(nvenc_.encoder, &lock_bitstream);
    if (status != NV_ENC_SUCCESS) {
        return false;
    }

    // Copy encoded data
    frame_data.resize(lock_bitstream.bitstreamSizeInBytes);
    std::memcpy(frame_data.data(), lock_bitstream.bitstreamBufferPtr, lock_bitstream.bitstreamSizeInBytes);

    // Unlock bitstream
    nvenc_.nv_enc.nvEncUnlockBitstream(nvenc_.encoder, lock_bitstream.outputBitstream);

    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.frames_encoded++;
        stats_.total_bytes += lock_bitstream.bitstreamSizeInBytes;
        updateStatistics(lock_bitstream.bitstreamSizeInBytes, 0.0); // TODO: Calculate actual latency
    }

    return true;
}

void FrameCapture::flush() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this]() { return frame_queue_.empty(); });
}

FrameCapture::Statistics FrameCapture::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool FrameCapture::copyImageToBuffer(VkCommandBuffer command_buffer, VkImage image) {
    // Begin command buffer
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        return false;
    }

    // Create image memory barrier
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    // Copy image to buffer
    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = config_.width;
    region.imageExtent.height = config_.height;
    region.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(command_buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vulkan_.staging_buffer,
        1,
        &region);

    // Create buffer memory barrier
    VkBufferMemoryBarrier buffer_barrier = {};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = vulkan_.staging_buffer;
    buffer_barrier.offset = 0;
    buffer_barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0, nullptr,
        1, &buffer_barrier,
        0, nullptr);

    // End command buffer
    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        return false;
    }

    // Submit command buffer
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    result = vkQueueSubmit(VK_NULL_HANDLE, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        return false;
    }

    // Wait for queue to idle
    result = vkQueueWaitIdle(VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool FrameCapture::encodeFrame(const void* frame_data) {
    // Map input buffer
    NV_ENC_MAP_INPUT_RESOURCE map_input_resource = {};
    map_input_resource.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    map_input_resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    map_input_resource.resource = (void*)cuda_.device_buffer;

    NVENCSTATUS status = nvenc_.nv_enc.nvEncMapInputResource(nvenc_.encoder, &map_input_resource);
    if (status != NV_ENC_SUCCESS) {
        return false;
    }

    // Encode frame
    NV_ENC_PIC_PARAMS pic_params = {};
    pic_params.version = NV_ENC_PIC_PARAMS_VER;
    pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    pic_params.inputBuffer = map_input_resource.mappedResource;
    pic_params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12_PL;
    pic_params.inputWidth = config_.width;
    pic_params.inputHeight = config_.height;
    pic_params.outputBitstream = nvenc_.output_buffers[0];
    pic_params.completionEvent = nullptr;

    status = nvenc_.nv_enc.nvEncEncodePicture(nvenc_.encoder, &pic_params);
    if (status != NV_ENC_SUCCESS) {
        nvenc_.nv_enc.nvEncUnmapInputResource(nvenc_.encoder, map_input_resource.mappedResource);
        return false;
    }

    // Unmap input buffer
    status = nvenc_.nv_enc.nvEncUnmapInputResource(nvenc_.encoder, map_input_resource.mappedResource);
    if (status != NV_ENC_SUCCESS) {
        return false;
    }

    return true;
}

void FrameCapture::updateStatistics(uint64_t bytes, double latency) {
    stats_.total_bytes += bytes;
    
    // Update average latency
    static constexpr double alpha = 0.1; // Smoothing factor
    stats_.average_latency = (1.0 - alpha) * stats_.average_latency + alpha * latency;
    
    // Update average FPS
    static auto last_update = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
    if (elapsed >= 1000) { // Update every second
        stats_.average_fps = static_cast<double>(stats_.frames_encoded) * 1000.0 / elapsed;
        stats_.frames_encoded = 0;
        last_update = now;
    }
}

void FrameCapture::encodeThread() {
    while (!should_stop_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this]() {
            return !frame_queue_.empty() || should_stop_;
        });

        if (should_stop_) {
            break;
        }

        FrameData frame = std::move(frame_queue_.front());
        frame_queue_.pop();
        lock.unlock();

        if (!encodeFrame(frame.data.data())) {
            // Handle encoding error
            continue;
        }
    }
}

} // namespace gpu
} // namespace anarchy 