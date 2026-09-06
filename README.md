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

The first configure needs internet access to fetch the pinned dependencies. Scene
rendering can later be recorded after `Renderer::begin_frame()` and before Chrome
records its ImGui draw data.

## Source layout

- `src/platform`: SDL window/event ownership and the Dear ImGui SDL adapter
- `src/chrome`: Dear ImGui core context, panel layout, and UI interaction state
- `src/graphics/scene_renderer.*`: the isolated offscreen viewport producer
- `src/graphics/vulkan_backend.*`: shared Vulkan state, swapchain presentation, and
  the Dear ImGui Vulkan adapter
- `src/application`: dependency-free orchestration policy connecting the layers

The scene renderer currently returns an empty viewport image, matching the starter's
intent not to draw scene content yet. Its output handle and viewport-size flow are in
place for adding the offscreen image and scene pipeline without coupling scene code
to the panel layout.
