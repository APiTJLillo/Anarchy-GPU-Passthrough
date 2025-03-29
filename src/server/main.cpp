#include "common/network/zmq_wrapper.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace anarchy::network;

int main() {
    try {
        // Create server
        ZMQWrapper server("tcp://*:5555", ZMQWrapper::Role::SERVER);
        
        // Set up message callback
        server.setMessageCallback([](const Message& msg) {
            std::cout << "Received message type: " << static_cast<int>(msg.header.type) << std::endl;
            std::cout << "Message size: " << msg.header.size << " bytes" << std::endl;
            std::cout << "Sequence: " << msg.header.sequence << std::endl;
            std::cout << "Compression: " << static_cast<int>(msg.header.compression) << std::endl;
            std::cout << "----------------------------------------" << std::endl;
        });
        
        // Set up error callback
        server.setErrorCallback([](const std::string& error) {
            std::cerr << "Error: " << error << std::endl;
        });
        
        // Start server
        if (!server.start()) {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }
        
        std::cout << "Server started on IP: " << server.getServerAddress() << std::endl;
        std::cout << "Listening for connections..." << std::endl;
        
        // Keep the server running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 