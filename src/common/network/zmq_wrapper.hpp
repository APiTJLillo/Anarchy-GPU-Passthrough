#pragma once

#include <zmq.hpp>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <zlib.h>
#include <lz4.h>

namespace anarchy::network {

enum class MessageType : uint8_t {
    HEARTBEAT = 0,
    COMMAND = 1,
    RESPONSE = 2,
    ERROR = 3,
    RECONNECT = 4
};

enum class CompressionType : uint8_t {
    NONE = 0,
    ZLIB = 1,
    LZ4 = 2
};

enum class CompressionLevel : uint8_t {
    FAST = 0,
    BALANCED = 1,
    MAX = 2
};

struct CompressionStats {
    size_t total_bytes_before{0};
    size_t total_bytes_after{0};
    size_t messages_compressed{0};
    size_t messages_decompressed{0};
    size_t compression_failures{0};
    size_t decompression_failures{0};
    double average_compression_ratio{0.0};
    std::chrono::milliseconds average_compression_time{0};
    std::chrono::milliseconds average_decompression_time{0};
};

struct MessageHeader {
    MessageType type;
    uint32_t size;
    uint64_t timestamp;
    uint32_t sequence_number;
    CompressionType compression;
    uint32_t original_size;  // For decompression
};

struct Message {
    MessageHeader header;
    std::vector<uint8_t> payload;
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
        RECONNECTING,
        ERROR
    };

    static constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1MB
    static constexpr uint32_t DEFAULT_CONNECTION_TIMEOUT = 5000;  // 5 seconds
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;  // 1 second
    static constexpr uint32_t MAX_RECONNECT_ATTEMPTS = 5;
    static constexpr uint32_t RECONNECT_DELAY_MS = 1000;  // 1 second
    static constexpr size_t COMPRESSION_THRESHOLD = 1024;  // 1KB
    static constexpr size_t NETWORK_SPEED_WINDOW = 1000;  // 1 second window for network speed measurement
    static constexpr double MIN_COMPRESSION_RATIO = 0.8;  // Minimum compression ratio to enable compression

    using MessageCallback = std::function<void(const Message&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using StateChangeCallback = std::function<void(ConnectionState)>;

    ZMQWrapper(const std::string& endpoint, Role role);
    ~ZMQWrapper();

    // Connection management
    bool connect(const std::string& address);
    void disconnect();
    bool isConnected() const;
    ConnectionState getConnectionState() const;

    // Message handling
    bool sendMessage(const Message& message);
    void setMessageCallback(MessageCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setStateChangeCallback(StateChangeCallback callback);

    // Configuration
    void setConnectionTimeout(uint32_t ms);
    void setMaxReconnectAttempts(uint32_t attempts);
    void setReconnectDelay(uint32_t ms);
    void setCompressionThreshold(size_t threshold);
    void setCompressionLevel(CompressionLevel level);
    void setCompressionType(CompressionType type);
    void enableAdaptiveCompression(bool enable);

    // Statistics
    size_t getBytesSent() const;
    size_t getBytesReceived() const;
    size_t getMessagesSent() const;
    size_t getMessagesReceived() const;
    uint32_t getCurrentLatency() const;
    CompressionStats getCompressionStats() const;
    double getCurrentNetworkSpeed() const;  // Bytes per second

    // Control
    void start();
    void stop();

private:
    void receiveLoop();
    void heartbeatLoop();
    void reconnectLoop();
    void processMessage(const Message& message);
    void handleError(const std::string& error);
    void updateConnectionState(ConnectionState new_state);
    bool attemptReconnect();
    void cleanup();

    // Compression helpers
    bool compressMessage(Message& message);
    bool decompressMessage(Message& message);
    size_t compressData(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size);
    size_t decompressData(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size);
    bool shouldCompressMessage(const Message& message) const;
    void updateCompressionStats(size_t original_size, size_t compressed_size, 
                              std::chrono::milliseconds compression_time);
    void updateNetworkSpeed(size_t bytes_transferred);
    CompressionType selectCompressionType(const Message& message) const;

    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> socket_;
    Role role_;
    std::string endpoint_;
    
    // Statistics
    std::atomic<size_t> bytes_sent_{0};
    std::atomic<size_t> bytes_received_{0};
    std::atomic<size_t> messages_sent_{0};
    std::atomic<size_t> messages_received_{0};
    std::atomic<uint32_t> current_latency_{0};
    
    // Compression settings
    std::atomic<CompressionType> compression_type_{CompressionType::ZLIB};
    std::atomic<CompressionLevel> compression_level_{CompressionLevel::BALANCED};
    std::atomic<bool> adaptive_compression_{true};
    std::atomic<size_t> compression_threshold_{COMPRESSION_THRESHOLD};
    
    // Compression statistics
    CompressionStats compression_stats_;
    std::mutex stats_mutex_;
    
    // Network speed measurement
    struct NetworkSpeedMeasurement {
        size_t bytes{0};
        std::chrono::steady_clock::time_point timestamp;
    };
    std::vector<NetworkSpeedMeasurement> network_speed_history_;
    std::mutex network_speed_mutex_;
    
    // Connection state
    std::atomic<ConnectionState> connection_state_{ConnectionState::DISCONNECTED};
    std::atomic<uint32_t> connection_timeout_{DEFAULT_CONNECTION_TIMEOUT};
    std::atomic<uint32_t> max_reconnect_attempts_{MAX_RECONNECT_ATTEMPTS};
    std::atomic<uint32_t> reconnect_delay_{RECONNECT_DELAY_MS};
    std::atomic<uint32_t> reconnect_attempts_{0};
    
    // Threading
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    std::thread receive_thread_;
    std::thread heartbeat_thread_;
    std::thread reconnect_thread_;
    
    // Callbacks
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    StateChangeCallback state_change_callback_;
    
    // Synchronization
    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<Message> message_queue_;
    
    // Timing
    std::chrono::steady_clock::time_point last_heartbeat_;
};

} // namespace anarchy::network 