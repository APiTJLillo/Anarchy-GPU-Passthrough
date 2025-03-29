#include "server/gpu_server.hpp"
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace anarchy {
namespace server {

GPUServer::GPUServer(const std::string& address)
    : vulkan_instance_(std::make_unique<gpu::VulkanUtils::Instance>())
    , vulkan_device_(std::make_unique<gpu::VulkanUtils::Device>(*vulkan_instance_))
    , zmq_(std::make_unique<network::ZMQWrapper>(address, network::ZMQWrapper::Role::SERVER))
    , running_(false)
{
    zmq_->start();
}

GPUServer::~GPUServer() {
    stop();
}

void GPUServer::start() {
    running_ = true;
}

void GPUServer::stop() {
    running_ = false;
}

} // namespace server
} // namespace anarchy 