#pragma once

#include <zmq.hpp>
#include <zlib.h>
#include <lz4.h>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <chrono>
#include <vector>
#include <condition_variable>
#include "common/network/protocol.hpp"

namespace anarchy {
namespace network {

enum class CompressionLevel {
    FAST,
    BALANCED,
    MAX
};

// Non-atomic version for returning stats
struct CompressionStatsData {
    size_t messages_compressed{0};
    size_t messages_decompressed{0};
    size_t total_bytes_before{0};
    size_t total_bytes_after{0};
    size_t compression_failures{0};
    size_t decompression_failures{0};
    double average_compression_ratio{0.0};
    std::chrono::microseconds average_compression_time{0};
    std::chrono::microseconds average_decompression_time{0};
};

// Internal atomic version
struct CompressionStats {
    std::atomic<size_t> messages_compressed{0};
    std::atomic<size_t> messages_decompressed{0};
    std::atomic<size_t> total_bytes_before{0};
    std::atomic<size_t> total_bytes_after{0};
    std::atomic<size_t> compression_failures{0};
    std::atomic<size_t> decompression_failures{0};
    std::atomic<double> average_compression_ratio{0.0};
    std::chrono::microseconds average_compression_time{0};
    std::chrono::microseconds average_decompression_time{0};

    // Convert to non-atomic version
    CompressionStatsData toData() const {
        CompressionStatsData data;
        data.messages_compressed = messages_compressed.load();
        data.messages_decompressed = messages_decompressed.load();
        data.total_bytes_before = total_bytes_before.load();
        data.total_bytes_after = total_bytes_after.load();
        data.compression_failures = compression_failures.load();
        data.decompression_failures = decompression_failures.load();
        data.average_compression_ratio = average_compression_ratio.load();
        data.average_compression_time = average_compression_time;
        data.average_decompression_time = average_decompression_time;
        return data;
    }
};

class ZMQWrapper {
public:
    enum class Role {
        SERVER,
        CLIENT
    };

    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING
    };

    using MessageCallback = std::function<void(const Message&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    static constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024 * 100;  // 100MB
    static constexpr uint32_t DEFAULT_CONNECTION_TIMEOUT = 5000;    // 5 seconds
    static constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL = 1000;   // 1 second
    static constexpr uint32_t DEFAULT_MAX_RECONNECT_ATTEMPTS = 5;
    static constexpr uint32_t DEFAULT_RECONNECT_DELAY = 1000;      // 1 second

    ZMQWrapper(const std::string& endpoint, Role role);
    ~ZMQWrapper();

    bool start();
    void stop();
    bool sendMessage(const Message& message);
    void setMessageCallback(MessageCallback callback);
    void setErrorCallback(ErrorCallback callback);
    bool isConnected() const;
    std::string getServerAddress() const;  // Get the server's IP address for client connections

    // Compression related methods
    void setCompressionType(CompressionType type);
    void setCompressionLevel(CompressionLevel level);
    void enableAdaptiveCompression(bool enable);
    CompressionStatsData getCompressionStats() const;
    double getCurrentNetworkSpeed() const;

private:
    void workerThread();
    void handleMessage(const Message& message);
    void handleError(const std::string& error);
    bool shouldCompressMessage(size_t message_size) const;
    CompressionType selectCompressionType(size_t message_size) const;
    size_t compressData(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size);
    size_t decompressData(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size);
    void updateNetworkSpeed(size_t bytes_sent);
    void updateCompressionStats(size_t before_size, size_t after_size, 
                              const std::chrono::microseconds& compression_time);
    void cleanupOldSpeedHistory() const;

    std::string endpoint_;
    Role role_;
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> socket_;
    std::thread worker_thread_;
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<ConnectionState> connection_state_{ConnectionState::DISCONNECTED};
    std::atomic<uint32_t> reconnect_attempts_{0};
    std::atomic<uint32_t> max_reconnect_attempts_{DEFAULT_MAX_RECONNECT_ATTEMPTS};
    std::atomic<uint32_t> reconnect_delay_{DEFAULT_RECONNECT_DELAY};
    std::atomic<uint32_t> connection_timeout_{DEFAULT_CONNECTION_TIMEOUT};
    std::atomic<size_t> messages_received_{0};
    std::atomic<uint32_t> current_latency_{0};
    std::chrono::steady_clock::time_point last_heartbeat_;
    size_t max_message_size_{MAX_MESSAGE_SIZE};

    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    mutable std::mutex callback_mutex_;

    // Compression related members
    CompressionType compression_type_{CompressionType::ZLIB};
    CompressionLevel compression_level_{CompressionLevel::BALANCED};
    bool adaptive_compression_{false};
    CompressionStats compression_stats_;
    mutable std::mutex compression_stats_mutex_;
    mutable std::mutex network_speed_mutex_;
    mutable std::deque<std::pair<std::chrono::steady_clock::time_point, size_t>> network_speed_history_;
};

} // namespace network
} // namespace anarchy 