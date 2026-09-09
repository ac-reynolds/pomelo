# Pomelo

A minimal C++20 starter for SDL3, Vulkan, and Dear ImGui. It opens a four-region editor shell with a rotating cube in the center viewport and empty left, right, and footer panels. Vulkan renders the cube into an offscreen color/depth target, then ImGui displays it in the viewport.

## Prerequisites

- CMake 3.24+
- A C++20 compiler
- Python 3 (for shader embedding during the build)
- Vulkan headers, loader, and a working Vulkan driver
- SPIR-V-enabled DXC (tested with v1.9.2607 on Linux)
- Git (SDL3 and ImGui are fetched during the first configure)

On Ubuntu/Debian, the Vulkan development dependency is typically `libvulkan-dev`. SDL3 and ImGui do not need to be installed system-wide.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/pomelo
```

The first configure needs internet access to fetch the pinned dependencies.
CMake uses DXC to compile HLSL from `shaders/` to SPIR-V, then runs
`deployment/embed_spirv.py` to generate C++ source files under `build/generated/Debug/`.
Shader source or script changes regenerate those files automatically, which are compiled into `scene_renderer`. The
executable includes the compiled shaders and can run from any working directory.

CMake discovers native `pomelo-dxc` or `dxc` on PATH or in the Vulkan SDK.
Override it with `-DPOMELO_DXC_EXECUTABLE=/absolute/path/to/dxc` (or `dxc.exe`
on Windows). The Linux installation uses `~/.local/bin/pomelo-dxc`. DXC is needed
only at build time. Generated files use separate directories per configuration.

The embedding script can also be run independently:

```sh
python3 deployment/embed_spirv.py build/generated/Debug/cube_vert.spv /tmp/cube_vert.cpp --symbol cube_vert_spirv --header graphics/shaders.hpp
```

## Source layout

- `src/chrome`: Dear ImGui core context, panel layout, and UI interaction state
- `src/graphics/scene_renderer.*`: the isolated offscreen viewport producer
- `src/graphics/vulkan_backend.*`: shared Vulkan state, swapchain presentation, and
  the Dear ImGui Vulkan adapter
- `src/application`: application loop, SDL window/event ownership, and the Dear ImGui SDL adapter
- `shaders`: cube vertex and fragment HLSL sources and shared stage interface
- `deployment`: build and deployment helper scripts
