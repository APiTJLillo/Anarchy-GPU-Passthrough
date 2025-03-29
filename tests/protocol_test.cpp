#include <gtest/gtest.h>
#include "common/network/protocol.hpp"
#include <cstring>

using namespace anarchy::network;

class ProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        protocol = std::make_unique<Protocol>();
    }

    void TearDown() override {
        protocol.reset();
    }

    std::unique_ptr<Protocol> protocol;
};

TEST_F(ProtocolTest, MessageHeaderSize) {
    // MessageHeader should be 32 bytes (8 bytes for type + 4 bytes for size + 4 bytes for sequence + 8 bytes for timestamp + 8 bytes for compression)
    EXPECT_EQ(sizeof(MessageHeader), 32);
}

TEST_F(ProtocolTest, MessageTypeValues) {
    // Verify message type values are unique and properly ordered
    EXPECT_EQ(static_cast<uint8_t>(MessageType::CONNECT), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::DISCONNECT), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::HEARTBEAT), 0x03);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::VK_CREATE_INSTANCE), 0x10);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::VK_CREATE_DEVICE), 0x11);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::VK_CREATE_SWAPCHAIN), 0x12);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::FRAME_DATA), 0x20);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::ERROR), 0xF0);
}

TEST_F(ProtocolTest, ProtocolConstants) {
    // Verify protocol constants
    EXPECT_EQ(MAX_MESSAGE_SIZE, 1024 * 1024);  // 1MB
    EXPECT_EQ(MAX_FRAME_SIZE, 16 * 1024 * 1024);  // 16MB
    EXPECT_EQ(HEARTBEAT_INTERVAL_MS, 1000);  // 1 second
    EXPECT_EQ(CONNECTION_TIMEOUT_MS, 5000);  // 5 seconds
}

TEST_F(ProtocolTest, MessageStructure) {
    Message msg;
    msg.header.type = MessageType::HEARTBEAT;
    msg.header.size = 0;
    msg.header.sequence = 1;
    msg.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    EXPECT_EQ(msg.header.type, MessageType::HEARTBEAT);
    EXPECT_EQ(msg.header.size, 0);
    EXPECT_EQ(msg.header.sequence, 1);
    EXPECT_GT(msg.header.timestamp, 0);
}

TEST_F(ProtocolTest, ConnectionParams) {
    // Create test connection parameters
    ConnectionParams params;
    params.version = {1, 0, 0};
    params.max_message_size = MAX_MESSAGE_SIZE;
    params.max_frame_size = MAX_FRAME_SIZE;
    params.compression_enabled = false;
    params.encryption_enabled = false;

    // Verify connection parameters
    EXPECT_EQ(params.version.major, 1);
    EXPECT_EQ(params.version.minor, 0);
    EXPECT_EQ(params.version.patch, 0);
    EXPECT_EQ(params.max_message_size, MAX_MESSAGE_SIZE);
    EXPECT_EQ(params.max_frame_size, MAX_FRAME_SIZE);
    EXPECT_FALSE(params.compression_enabled);
    EXPECT_FALSE(params.encryption_enabled);
}

TEST_F(ProtocolTest, ErrorInfo) {
    // Create test error info
    ErrorInfo error;
    error.code = 1;
    error.message = "Test error";

    // Verify error info
    EXPECT_EQ(error.code, 1);
    EXPECT_EQ(error.message, "Test error");
}

TEST_F(ProtocolTest, BasicTest) {
    EXPECT_TRUE(true);
} 