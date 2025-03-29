#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <variant>

namespace anarchy {
namespace network {

// Message types for different GPU operations
enum class MessageType : uint8_t {
    // Connection management
    CONNECT = 0x01,
    DISCONNECT = 0x02,
    HEARTBEAT = 0x03,

    // Vulkan operations
    VK_CREATE_INSTANCE = 0x10,
    VK_CREATE_DEVICE = 0x11,
    VK_CREATE_SWAPCHAIN = 0x12,
    VK_CREATE_COMMAND_POOL = 0x13,
    VK_CREATE_COMMAND_BUFFER = 0x14,
    VK_BEGIN_COMMAND_BUFFER = 0x15,
    VK_END_COMMAND_BUFFER = 0x16,
    VK_QUEUE_SUBMIT = 0x17,
    VK_ACQUIRE_NEXT_IMAGE = 0x18,
    VK_PRESENT = 0x19,

    // Frame operations
    FRAME_DATA = 0x20,
    FRAME_ACK = 0x21,
    FRAME_REQUEST = 0x22,

    // Error handling
    ERROR = 0xF0,
    RESET = 0xF1
};

enum class CompressionType {
    NONE,
    ZLIB,
    LZ4
};

// Message header structure
struct MessageHeader {
    MessageType type;
    uint32_t size;  // Size of the payload
    uint32_t sequence;  // For tracking message order
    uint64_t timestamp;  // For latency measurement
    CompressionType compression{CompressionType::NONE};  // Compression type used for payload
};

// Protocol version information
struct ProtocolVersion {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};

// Connection parameters
struct ConnectionParams {
    ProtocolVersion version;
    uint32_t max_message_size;
    uint32_t max_frame_size;
    bool compression_enabled;
    bool encryption_enabled;
};

// Error information
struct ErrorInfo {
    uint32_t code;
    std::string message;
};

// Message structure combining header and payload
struct Message {
    MessageHeader header;
    std::vector<uint8_t> payload;
};

// Protocol constants
constexpr uint32_t MAX_MESSAGE_SIZE = 1024 * 1024;  // 1MB
constexpr uint32_t MAX_FRAME_SIZE = 16 * 1024 * 1024;  // 16MB
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;  // 1 second
constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;  // 5 seconds

class Protocol {
public:
    Protocol();
    ~Protocol();
};

} // namespace network
} // namespace anarchy 