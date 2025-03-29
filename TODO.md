# Anarchy GPU Passthrough Project

## 🚩 Project Overview

### Objective
Build a real-time GPU-over-network solution allowing Windows (Legion Go) apps/games to transparently leverage an NVIDIA RTX 4090 GPU hosted on a Linux machine (P16) over Thunderbolt/USB4 networking.

### How it Works (High-level)
1. On Windows (Legion Go), a lightweight client driver/layer intercepts DirectX/Vulkan API calls
2. The intercepted calls are serialized and forwarded over Thunderbolt to a server on Linux (P16)
3. Linux server executes these commands on the RTX 4090 GPU
4. Rendered frames are captured, optionally compressed/encoded, and streamed back over Thunderbolt
5. Client decodes and presents frames locally, giving an illusion of a local GPU

## 🛠️ Tech Stack Recommendation

| Component | Technology Recommendations |
|-----------|---------------------------|
| Networking Transport | ZeroMQ or raw TCP sockets |
| GPU Command Forwarding | Custom Vulkan ICD (DLL), DXGI DLL Shim |
| GPU Rendering Backend | Vulkan API (NVIDIA Driver), CUDA if needed |
| Frame Capture | Vulkan framebuffer readback, NVIDIA NVENC |
| Frame Encoding | NVENC (H.264/HEVC/AV1) |
| Frame Decoding | Windows DirectX Media Foundation (DXMF), NVDEC |
| Windows Client Driver | Windows ICD layers, DXGI/DX hooks, or custom virtual GPU driver |
| Linux Server Language | Modern C++ (performance-critical), Rust (optional safety), Python (prototyping) |
| Build System | CMake, vcpkg or Conan for dependencies |
| Performance Monitoring | NVIDIA Nsight Systems, RenderDoc, Wireshark |
| GUI / Configuration | Qt (optional), CLI for simplicity first |
| OS Compatibility | Linux Server (Ubuntu/Debian preferred), Windows 11 client |

## ✅ Detailed Project Roadmap & Todo List

### 🚀 Stage 1: Setup and Prototyping (MVP)

#### Thunderbolt Networking
- [ ] Establish Thunderbolt network (Linux↔Windows)
- [ ] Configure static IP addresses (10.x.x.x) and test speed (iperf3)
- [ ] Confirm stable low-latency (<2ms) and high-throughput (>10Gbps)

#### Basic Vulkan Client-Server Prototype
- [x] Develop minimal Linux server that accepts simple Vulkan commands over ZeroMQ/TCP
  - [x] Implement ZMQWrapper class with PUB/SUB pattern
  - [x] Add message protocol with header and payload support
  - [x] Implement heartbeat mechanism for connection monitoring
  - [x] Add comprehensive test suite for network layer
  - [ ] Add error handling and reconnection logic
  - [ ] Add message compression for large payloads
- [ ] Confirm Vulkan rendering works (render to off-screen framebuffer)
- [ ] Capture framebuffer and return raw pixels to client
- [ ] Develop simple Windows client that sends basic Vulkan commands (draw triangle)
- [ ] Display rendered image in a basic window on Windows

**MVP Goal**: Simple Vulkan-rendered triangle remotely displayed

### 🚀 Stage 2: Expanded Functionality (Gaming and Frame Streaming)

#### Robust Command Forwarding
- [ ] Expand client/server protocol to support complex Vulkan commands
- [ ] Support synchronization primitives (fences, semaphores)

#### Frame Encoding (Server-side)
- [ ] Implement framebuffer capture via Vulkan APIs
- [ ] Integrate NVENC for hardware-accelerated encoding (H.265 or AV1)
- [ ] Optimize latency: keep encoding time <5ms per frame

#### Frame Decoding (Client-side)
- [ ] Implement hardware-accelerated decoding (DXMF/NVDEC)
- [ ] Display decoded frames smoothly at ≥60FPS

#### Performance Measurement
- [ ] Measure round-trip latency (render+encode+network+decode+display)
- [ ] Aim for total latency <20ms per frame

**Stage Goal**: Run a Vulkan benchmark or game (e.g. DOOM Eternal Vulkan) remotely

### 🚀 Stage 3: Support DirectX Applications (Windows Gaming)

#### DirectX Compatibility Layer
- [ ] Implement DXGI shim (dxgi.dll) or leverage DXVK for DirectX→Vulkan conversion
- [ ] Validate interception for popular DirectX games
- [ ] Forward translated Vulkan commands to Linux server

#### Optimization
- [ ] Reduce overhead introduced by DirectX→Vulkan conversion
- [ ] Benchmark popular games, ensure playable framerates

**Stage Goal**: Play AAA DirectX titles remotely at smooth performance

### 🚀 Stage 4: Polishing and Enhancements

#### Bidirectional Input Support
- [ ] Client forwards gamepad/keyboard/mouse input to server

#### Adaptive Quality Control
- [ ] Dynamic frame quality adjustments based on latency/bandwidth

#### GUI/CLI
- [ ] User-friendly configuration GUI (optional, Qt recommended)
- [ ] Simple CLI tool for power-users

#### Security and Stability
- [ ] Implement proper authentication and encryption (optional, for local-only)
- [ ] Error handling, reconnect, and graceful failure

#### Open-Source Release Preparation
- [ ] Document extensively (readme, setup, FAQ)
- [ ] Dockerize/Linux packages for ease of installation
- [ ] Windows installer/package

**Final Goal**: Polished, reliable, easily deployable solution with documentation and installer

## 📚 Detailed Guidance by Component

### 📌 Thunderbolt Networking (Setup)
- Linux Kernel drivers (thunderbolt, boltctl) to establish connections
- Windows Thunderbolt Networking built-in (appears as Ethernet)

Test setup:
```bash
# Linux side (P16)
sudo ip addr add 10.0.0.1/24 dev thunderbolt0
sudo ip link set dev thunderbolt0 up

# Windows side:
# Static IP (e.g., 10.0.0.2/24) on Thunderbolt interface
```

### 📌 Client Vulkan ICD Layer (Intercepting Vulkan Calls)
- Create Vulkan ICD JSON (custom DLL layer)
- Forward Vulkan calls via network to Linux server

Resources:
- [Vulkan Layer Guide](https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/docs/Layer_Application_Developer_Guide.md)
- Reference RenderDoc (source available) for intercepting Vulkan calls

### 📌 GPU Server (Linux, Vulkan/NVIDIA)
- Write server application using Vulkan SDK for Linux
- Accept API calls (command buffers) and execute via Vulkan
- Read back framebuffers (vkCmdCopyImageToBuffer) → encode using NVENC

Useful tools:
- Vulkan SDK & Samples
- NVIDIA NVENC API

### 📌 Networking Protocol (ZeroMQ/TCP)
- Use ZeroMQ for rapid prototyping
- Later optimize with custom binary protocol if latency sensitive

Example ZeroMQ setup (simple):
- PUB/SUB or REQ/REP patterns for easy initial setup

### 📌 Frame Encoding/Decoding (NVENC/NVDEC)
- On Linux, use NVIDIA's SDK examples to encode frames efficiently
- On Windows, decode frames using built-in DirectX Media Foundation (fast GPU decoding)

NVIDIA SDK docs:
- [Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)

### 📌 Windows DX Compatibility (DXVK/DXGI shims)
- Use DXVK (Vulkan-based DX11/DX12 translation layer)
- Alternatively, custom DXGI DLL (dxgi.dll) injection to redirect DX calls to Vulkan ICD

Reference Projects:
- [DXVK](https://github.com/doitsujin/dxvk)
- [Microsoft Detours](https://github.com/microsoft/Detours)

## 🧑‍💻 Development Tips
- Prototype small pieces first
- Start single-threaded, add concurrency later
- Benchmark every step (network, GPU, encode/decode)
- Regularly test end-to-end latency

## 🎉 Final Outcome
When complete, you'll have built a cutting-edge, open-source GPU-over-USB4 solution that's unique and highly valuable to both gamers and portable computing enthusiasts.