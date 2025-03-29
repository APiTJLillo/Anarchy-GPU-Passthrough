#include <gtest/gtest.h>
#include "common/network/zmq_wrapper.hpp"
#include "common/network/protocol.hpp"
#include <thread>
#include <chrono>
#include <iostream>
#include <atomic>

using namespace anarchy::network;

class NetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<ZMQWrapper>("tcp://*:5555", ZMQWrapper::Role::SERVER);
        client = std::make_unique<ZMQWrapper>("tcp://127.0.0.1:5555", ZMQWrapper::Role::CLIENT);
        
        // Set up message callback
        server->setMessageCallback([this](const Message& msg) {
            received_messages.push_back(msg);
        });
        
        client->setMessageCallback([this](const Message& msg) {
            received_messages.push_back(msg);
        });
        
        // Set up error callback
        server->setErrorCallback([this](const std::string& error) {
            error_messages.push_back(error);
        });
        
        client->setErrorCallback([this](const std::string& error) {
            error_messages.push_back(error);
        });
        
        // Start both server and client
        server->start();
        client->start();
        
        // Wait for connection to establish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void TearDown() override {
        if (client) client->stop();
        if (server) server->stop();
        
        // Wait for threads to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        client.reset();
        server.reset();
        
        received_messages.clear();
        error_messages.clear();
    }
    
    std::unique_ptr<ZMQWrapper> server;
    std::unique_ptr<ZMQWrapper> client;
    std::vector<Message> received_messages;
    std::vector<std::string> error_messages;
};

TEST_F(NetworkTest, InitialConnection) {
    EXPECT_TRUE(server->isConnected());
    EXPECT_TRUE(client->isConnected());
}

TEST_F(NetworkTest, MessageExchange) {
    Message test_msg;
    test_msg.header.type = MessageType::FRAME_DATA;
    test_msg.header.size = 5;
    test_msg.header.sequence = 1;
    test_msg.header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    test_msg.payload = std::vector<uint8_t>{'H', 'e', 'l', 'l', 'o'};
    
    EXPECT_TRUE(client->sendMessage(test_msg));
    
    // Wait for message to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    ASSERT_FALSE(received_messages.empty());
    EXPECT_EQ(received_messages[0].header.type, MessageType::FRAME_DATA);
    EXPECT_EQ(received_messages[0].header.size, 5);
    EXPECT_EQ(received_messages[0].payload, test_msg.payload);
}

TEST_F(NetworkTest, Heartbeat) {
    // Wait for heartbeat messages
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    bool found_heartbeat = false;
    for (const auto& msg : received_messages) {
        if (msg.header.type == MessageType::HEARTBEAT) {
            found_heartbeat = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_heartbeat);
}

TEST_F(NetworkTest, LargeMessage) {
    Message large_msg;
    large_msg.header.type = MessageType::FRAME_DATA;
    large_msg.header.size = ZMQWrapper::MAX_MESSAGE_SIZE + 1;
    large_msg.header.sequence = 1;
    large_msg.header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    large_msg.payload.resize(ZMQWrapper::MAX_MESSAGE_SIZE + 1);
    
    EXPECT_FALSE(client->sendMessage(large_msg));
    
    // Check error message
    ASSERT_FALSE(error_messages.empty());
    EXPECT_TRUE(error_messages.back().find("Message size exceeds limit") != std::string::npos);
}

TEST_F(NetworkTest, Compression) {
    // Create a large message with repeating data (good for compression)
    Message test_msg;
    test_msg.header.type = MessageType::FRAME_DATA;
    test_msg.header.size = 1024 * 10;  // 10KB
    test_msg.header.sequence = 1;
    test_msg.header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    test_msg.payload.resize(test_msg.header.size);
    
    // Fill with repeating pattern
    for (size_t i = 0; i < test_msg.payload.size(); ++i) {
        test_msg.payload[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Enable compression
    client->setCompressionType(CompressionType::ZLIB);
    client->setCompressionLevel(CompressionLevel::MAX);
    client->enableAdaptiveCompression(true);
    
    // Send message
    EXPECT_TRUE(client->sendMessage(test_msg));
    
    // Wait for message to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify message was received and decompressed correctly
    ASSERT_FALSE(received_messages.empty());
    const Message& received = received_messages.back();
    EXPECT_EQ(received.header.type, MessageType::FRAME_DATA);
    EXPECT_EQ(received.header.size, test_msg.header.size);
    EXPECT_EQ(received.payload, test_msg.payload);
}

TEST_F(NetworkTest, CompressionStats) {
    // Create a large message with repeating data
    Message test_msg;
    test_msg.header.type = MessageType::FRAME_DATA;
    test_msg.header.size = 1024 * 10;  // 10KB
    test_msg.header.sequence = 1;
    test_msg.header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    test_msg.payload.resize(test_msg.header.size);
    
    // Fill with repeating pattern
    for (size_t i = 0; i < test_msg.payload.size(); ++i) {
        test_msg.payload[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Enable compression
    client->setCompressionType(CompressionType::ZLIB);
    client->setCompressionLevel(CompressionLevel::MAX);
    
    // Send message
    EXPECT_TRUE(client->sendMessage(test_msg));
    
    // Wait for message to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check compression statistics
    CompressionStatsData stats = client->getCompressionStats();
    EXPECT_GT(stats.messages_compressed, 0);
    EXPECT_GT(stats.total_bytes_before, 0);
    EXPECT_GT(stats.total_bytes_after, 0);
    EXPECT_GT(stats.average_compression_ratio, 0.0);
    EXPECT_GT(stats.average_compression_time.count(), 0);
}

TEST_F(NetworkTest, NetworkSpeed) {
    // Create a large message
    Message test_msg;
    test_msg.header.type = MessageType::FRAME_DATA;
    test_msg.header.sequence = 1;
    test_msg.header.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Create a 100KB payload
    const size_t payload_size = 1024 * 100;
    test_msg.payload.resize(payload_size);
    test_msg.header.size = payload_size;
    
    // Fill with random data
    for (size_t i = 0; i < test_msg.payload.size(); ++i) {
        test_msg.payload[i] = static_cast<uint8_t>(rand() % 256);
    }
    
    // Enable adaptive compression
    client->enableAdaptiveCompression(true);
    
    // Set up server response callback
    server->setMessageCallback([this](const Message& msg) {
        // Send acknowledgment back to client
        Message ack;
        ack.header.type = MessageType::FRAME_ACK;
        ack.header.sequence = msg.header.sequence;
        ack.header.size = 0;
        server->sendMessage(ack);
    });
    
    // Send message multiple times
    for (int i = 0; i < 5; ++i) {
        test_msg.header.sequence = i + 1;
        EXPECT_TRUE(client->sendMessage(test_msg));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Wait for all messages to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check network speed
    double speed = client->getCurrentNetworkSpeed();
    EXPECT_GT(speed, 0.0);
} 