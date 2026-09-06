# Pomelo

A minimal C++20 starter for SDL3, Vulkan, and Dear ImGui. It opens a four-region editor shell with empty left, center viewport, right, and footer panels. Vulkan owns the instance, device, presentation queue, swapchain, render pass, command buffers, and synchronization; only ImGui is drawn for now.

## Prerequisites

- CMake 3.24+
- A C++20 compiler
- Vulkan headers, loader, and a working Vulkan driver
- Git (SDL3 and ImGui are fetched during the first configure)

On Ubuntu/Debian, the Vulkan development dependency is typically `libvulkan-dev`. SDL3 and ImGui do not need to be installed system-wide.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/pomelo
```

The first configure needs internet access to fetch the pinned dependencies. Scene rendering can later be recorded inside the render pass in `VulkanContext::render`, before the ImGui draw call.
