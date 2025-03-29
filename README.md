# Anarchy GPU Passthrough

A real-time GPU-over-network solution allowing Windows applications to leverage remote NVIDIA GPUs over Thunderbolt/USB4 networking.

## Overview

Anarchy GPU Passthrough enables Windows applications (running on devices like the Legion Go) to transparently use an NVIDIA RTX 4090 GPU hosted on a Linux machine over Thunderbolt/USB4 networking. This project aims to provide a low-latency, high-performance solution for GPU passthrough without requiring complex virtualization setups.

## Features

- Real-time GPU command forwarding over Thunderbolt/USB4
- Support for Vulkan and DirectX applications
- Hardware-accelerated frame encoding/decoding
- Low-latency streaming (<20ms total latency)
- Bidirectional input support
- Adaptive quality control

## Requirements

### Linux Server (Host)
- Linux distribution (Ubuntu/Debian recommended)
- NVIDIA GPU with Vulkan support
- Thunderbolt/USB4 port
- CMake 3.20 or higher
- C++20 compatible compiler
- Vulkan SDK
- CUDA Toolkit
- ZeroMQ
- OpenSSL

### Windows Client
- Windows 11
- Thunderbolt/USB4 port
- Visual Studio 2019 or higher (for building)
- Vulkan SDK
- DirectX SDK

## Building from Source

### Linux Server

1. Install dependencies:
```bash
sudo apt update
sudo apt install build-essential cmake vulkan-sdk libzmq3-dev libssl-dev
```

2. Clone the repository:
```bash
git clone https://github.com/APiTJLillo/Anarchy-GPU-Passthrough.git
cd Anarchy-GPU-Passthrough
```

3. Build the project:
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Windows Client

1. Install Visual Studio 2019 or higher with C++ development tools
2. Install Vulkan SDK and DirectX SDK
3. Clone the repository
4. Open the solution in Visual Studio and build

## Usage

### Setting up Thunderbolt Network

1. Connect the Windows client to the Linux server via Thunderbolt/USB4
2. On Linux:
```bash
sudo ip addr add 10.0.0.1/24 dev thunderbolt0
sudo ip link set dev thunderbolt0 up
```

3. On Windows:
- Set static IP (10.0.0.2/24) on Thunderbolt interface

### Running the Server

```bash
./anarchy_server
```

### Installing the Client

1. Copy the built DLL to the Windows system directory or application directory
2. Configure the application to use the Anarchy GPU Passthrough ICD

## Development Status

This project is currently in early development. See [TODO.md](TODO.md) for the current roadmap and development status.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.