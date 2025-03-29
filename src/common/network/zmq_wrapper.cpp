#include "common/network/zmq_wrapper.hpp"
#include <zmq.hpp>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <netdb.h>
#include <arpa/inet.h>

// Define LZ4 acceleration constants if not defined
#ifndef LZ4_ACCELERATION_DEFAULT
#define LZ4_ACCELERATION_DEFAULT 1
#endif

#ifndef LZ4_ACCELERATION_MAX
#define LZ4_ACCELERATION_MAX 65537
#endif

namespace anarchy {
namespace network {

ZMQWrapper::ZMQWrapper(const std::string& endpoint, Role role)
    : endpoint_(endpoint)
    , role_(role)
    , context_(1)
    , last_heartbeat_(std::chrono::steady_clock::now())
{
    try {
        socket_ = std::make_unique<zmq::socket_t>(context_, role == Role::SERVER ? ZMQ_REP : ZMQ_REQ);
        
        if (role == Role::SERVER) {
            socket_->bind(endpoint);
        } else {
            socket_->connect(endpoint);
        }
        
        // Set socket options
        int linger = 0;
        socket_->set(zmq::sockopt::linger, linger);
        socket_->set(zmq::sockopt::rcvtimeo, static_cast<int>(DEFAULT_CONNECTION_TIMEOUT));
        socket_->set(zmq::sockopt::sndtimeo, static_cast<int>(DEFAULT_CONNECTION_TIMEOUT));
        
        connection_state_ = ConnectionState::CONNECTED;
        connected_ = true;
    } catch (const zmq::error_t& e) {
        handleError("Failed to initialize socket: " + std::string(e.what()));
        connection_state_ = ConnectionState::DISCONNECTED;
        connected_ = false;
    }
}

ZMQWrapper::~ZMQWrapper() {
    stop();
}

bool ZMQWrapper::start() {
    if (worker_thread_.joinable()) {
        return false;
    }
    
    should_stop_ = false;
    worker_thread_ = std::thread(&ZMQWrapper::workerThread, this);
    return true;
}

void ZMQWrapper::stop() {
    should_stop_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    if (socket_) {
        try {
            if (role_ == Role::SERVER) {
                socket_->unbind(endpoint_);
            } else {
                socket_->disconnect(endpoint_);
            }
        } catch (const zmq::error_t& e) {
            // Log error but continue cleanup
            handleError("Error during socket cleanup: " + std::string(e.what()));
        }
        socket_.reset();
    }
    
    connection_state_ = ConnectionState::DISCONNECTED;
    connected_ = false;
}

bool ZMQWrapper::sendMessage(const Message& message) {
    if (!connected_) {
        handleError("Cannot send message: not connected");
        return false;
    }
    
    // Check message size
    if (message.header.size > max_message_size_) {
        handleError("Message size exceeds limit");
        return false;
    }
    
    try {
        Message msg = message;
        size_t original_size = msg.payload.size();
        
        // Check if we should compress the message
        if (shouldCompressMessage(original_size)) {
            auto start_time = std::chrono::steady_clock::now();
            
            // Allocate buffer for compressed data
            std::vector<uint8_t> compressed_data(original_size);
            size_t compressed_size = compressData(msg.payload.data(), original_size,
                                                compressed_data.data(), compressed_data.size());
            
            if (compressed_size > 0 && compressed_size < original_size) {
                auto end_time = std::chrono::steady_clock::now();
                auto compression_time = std::chrono::duration_cast<std::chrono::microseconds>(
                    end_time - start_time);
                
                compressed_data.resize(compressed_size);
                msg.payload = std::move(compressed_data);
                msg.header.compression = selectCompressionType(original_size);
                
                updateCompressionStats(original_size, compressed_size, compression_time);
            }
        }
        
        // Send the message
        zmq::message_t header_msg(&msg.header, sizeof(MessageHeader));
        zmq::message_t payload_msg(msg.payload.data(), msg.payload.size());
        
        if (!socket_->send(header_msg, zmq::send_flags::sndmore)) {
            handleError("Failed to send message header");
            return false;
        }
        
        if (!socket_->send(payload_msg, zmq::send_flags::none)) {
            handleError("Failed to send message payload");
            return false;
        }
        
        updateNetworkSpeed(sizeof(MessageHeader) + msg.payload.size());
        return true;
        
    } catch (const zmq::error_t& e) {
        handleError("Error sending message: " + std::string(e.what()));
        return false;
    }
}

void ZMQWrapper::setMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = std::move(callback);
}

void ZMQWrapper::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = std::move(callback);
}

bool ZMQWrapper::isConnected() const {
    return connected_;
}

std::string ZMQWrapper::getServerAddress() const {
    if (role_ != Role::SERVER) {
        return "";
    }
    
    // Get the server's IP address
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;  // IPv4
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            freeaddrinfo(result);
            return std::string(ip);
        }
    }
    
    return "127.0.0.1";  // Fallback to localhost
}

void ZMQWrapper::setCompressionType(CompressionType type) {
    compression_type_ = type;
}

void ZMQWrapper::setCompressionLevel(CompressionLevel level) {
    compression_level_ = level;
}

void ZMQWrapper::enableAdaptiveCompression(bool enable) {
    adaptive_compression_ = enable;
}

CompressionStatsData ZMQWrapper::getCompressionStats() const {
    std::lock_guard<std::mutex> lock(compression_stats_mutex_);
    return compression_stats_.toData();
}

double ZMQWrapper::getCurrentNetworkSpeed() const {
    std::lock_guard<std::mutex> lock(network_speed_mutex_);
    cleanupOldSpeedHistory();
    
    // Calculate current speed in bytes per second
    size_t total_bytes = 0;
    for (const auto& entry : network_speed_history_) {
        total_bytes += entry.second;
    }
    
    return static_cast<double>(total_bytes);
}

void ZMQWrapper::cleanupOldSpeedHistory() const {
    auto now = std::chrono::steady_clock::now();
    auto window_start = now - std::chrono::seconds(1);
    
    // Remove old entries
    network_speed_history_.erase(
        std::remove_if(network_speed_history_.begin(), network_speed_history_.end(),
                      [window_start](const auto& m) {
                          return m.first < window_start;
                      }),
        network_speed_history_.end()
    );
}

void ZMQWrapper::workerThread() {
    while (!should_stop_) {
        try {
            // Check if we need to send a heartbeat
            auto now = std::chrono::steady_clock::now();
            if (now - last_heartbeat_ >= std::chrono::milliseconds(DEFAULT_HEARTBEAT_INTERVAL)) {
                Message heartbeat;
                heartbeat.header.type = MessageType::HEARTBEAT;
                heartbeat.header.size = 0;
                heartbeat.header.sequence = messages_received_;
                heartbeat.header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                    now.time_since_epoch()).count();
                
                if (sendMessage(heartbeat)) {
                    last_heartbeat_ = now;
                }
            }
            
            // Try to receive a message with a short timeout
            zmq::message_t header_msg;
            if (!socket_->recv(header_msg, zmq::recv_flags::dontwait)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            MessageHeader* header = static_cast<MessageHeader*>(header_msg.data());
            
            zmq::message_t payload_msg;
            if (!socket_->recv(payload_msg, zmq::recv_flags::none)) {
                handleError("Failed to receive message payload");
                continue;
            }
            
            Message msg;
            msg.header = *header;
            msg.payload.resize(payload_msg.size());
            std::memcpy(msg.payload.data(), payload_msg.data(), payload_msg.size());
            
            if (msg.header.compression != CompressionType::NONE) {
                auto start_time = std::chrono::steady_clock::now();
                
                std::vector<uint8_t> decompressed_data(msg.header.size);
                size_t decompressed_size = decompressData(msg.payload.data(), msg.payload.size(),
                                                        decompressed_data.data(), decompressed_data.size());
                
                if (decompressed_size > 0) {
                    auto end_time = std::chrono::steady_clock::now();
                    auto decompression_time = std::chrono::duration_cast<std::chrono::microseconds>(
                        end_time - start_time);
                    
                    decompressed_data.resize(decompressed_size);
                    msg.payload = std::move(decompressed_data);
                    msg.header.compression = CompressionType::NONE;
                    
                    updateCompressionStats(msg.payload.size(), payload_msg.size(), decompression_time);
                } else {
                    handleError("Failed to decompress message");
                    std::lock_guard<std::mutex> lock(compression_stats_mutex_);
                    compression_stats_.decompression_failures++;
                    continue;
                }
            }
            
            handleMessage(msg);
            messages_received_++;
            
        } catch (const zmq::error_t& e) {
            handleError("Error in worker thread: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void ZMQWrapper::handleMessage(const Message& message) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (message_callback_) {
        message_callback_(message);
    }
}

void ZMQWrapper::handleError(const std::string& error) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) {
        error_callback_(error);
    }
}

bool ZMQWrapper::shouldCompressMessage(size_t message_size) const {
    if (!adaptive_compression_) {
        return message_size > 1024;  // Compress messages larger than 1KB
    }
    
    double network_speed = getCurrentNetworkSpeed();
    return message_size > 1024 && network_speed < message_size * 10;
}

CompressionType ZMQWrapper::selectCompressionType(size_t message_size) const {
    if (compression_type_ != CompressionType::NONE) {
        return compression_type_;
    }
    
    // Use ZLIB for smaller messages (better compression ratio)
    // Use LZ4 for larger messages (faster compression)
    return message_size < 1024 * 1024 ? CompressionType::ZLIB : CompressionType::LZ4;
}

size_t ZMQWrapper::compressData(const uint8_t* input, size_t input_size,
                               uint8_t* output, size_t output_size) {
    if (compression_type_ == CompressionType::LZ4) {
        int compression_level;
        switch (compression_level_) {
            case CompressionLevel::FAST:
                compression_level = LZ4_ACCELERATION_MAX;
                break;
            case CompressionLevel::MAX:
                compression_level = 1;  // Best compression
                break;
            default:
                compression_level = LZ4_ACCELERATION_DEFAULT;
                break;
        }
        
        return LZ4_compress_fast(reinterpret_cast<const char*>(input),
                               reinterpret_cast<char*>(output),
                               static_cast<int>(input_size),
                               static_cast<int>(output_size),
                               compression_level);
    } else {
        // ZLIB compression
        z_stream stream;
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        
        int level;
        switch (compression_level_) {
            case CompressionLevel::FAST:
                level = Z_BEST_SPEED;
                break;
            case CompressionLevel::MAX:
                level = Z_BEST_COMPRESSION;
                break;
            default:
                level = Z_DEFAULT_COMPRESSION;
                break;
        }
        
        if (deflateInit(&stream, level) != Z_OK) {
            return 0;
        }
        
        stream.next_in = const_cast<uint8_t*>(input);
        stream.avail_in = static_cast<uInt>(input_size);
        stream.next_out = output;
        stream.avail_out = static_cast<uInt>(output_size);
        
        int ret = deflate(&stream, Z_FINISH);
        deflateEnd(&stream);
        
        if (ret != Z_STREAM_END) {
            return 0;
        }
        
        return output_size - stream.avail_out;
    }
}

size_t ZMQWrapper::decompressData(const uint8_t* input, size_t input_size,
                                 uint8_t* output, size_t output_size) {
    if (compression_type_ == CompressionType::LZ4) {
        return LZ4_decompress_safe(reinterpret_cast<const char*>(input),
                                 reinterpret_cast<char*>(output),
                                 static_cast<int>(input_size),
                                 static_cast<int>(output_size));
    } else {
        // ZLIB decompression
        z_stream stream;
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        stream.avail_in = 0;
        stream.next_in = Z_NULL;
        
        if (inflateInit(&stream) != Z_OK) {
            return 0;
        }
        
        stream.next_in = const_cast<uint8_t*>(input);
        stream.avail_in = static_cast<uInt>(input_size);
        stream.next_out = output;
        stream.avail_out = static_cast<uInt>(output_size);
        
        int ret = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        
        if (ret != Z_STREAM_END) {
            return 0;
        }
        
        return output_size - stream.avail_out;
    }
}

void ZMQWrapper::updateNetworkSpeed(size_t bytes_sent) {
    std::lock_guard<std::mutex> lock(network_speed_mutex_);
    network_speed_history_.emplace_back(std::chrono::steady_clock::now(), bytes_sent);
}

void ZMQWrapper::updateCompressionStats(size_t before_size, size_t after_size,
                                      const std::chrono::microseconds& compression_time) {
    std::lock_guard<std::mutex> lock(compression_stats_mutex_);
    
    size_t msg_count = compression_stats_.messages_compressed.load() + 1;
    compression_stats_.messages_compressed++;
    compression_stats_.total_bytes_before += before_size;
    compression_stats_.total_bytes_after += after_size;
    
    // Update average compression ratio
    double total_ratio = compression_stats_.average_compression_ratio.load() * (msg_count - 1);
    total_ratio += static_cast<double>(after_size) / before_size;
    compression_stats_.average_compression_ratio = total_ratio / msg_count;
    
    // Update average compression time
    auto total_time = compression_stats_.average_compression_time * (msg_count - 1);
    total_time += compression_time;
    compression_stats_.average_compression_time = total_time / msg_count;
}

} // namespace network
} // namespace anarchy 