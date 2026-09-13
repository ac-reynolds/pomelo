# Pomelo

A C++20 application using SDL3, Vulkan, and Dear ImGui. A compute shader advects a passive density field through a fixed velocity field, and a full-screen ray marcher renders the resulting volume. Collapsible simulation and rendering panels overlay the viewport, with a one-line status bar below.

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
Shader source or script changes regenerate those files automatically, which are compiled into `graphics`. The
executable includes the compiled shaders and can run from any working directory.

CMake discovers native `pomelo-dxc` or `dxc` on PATH or in the Vulkan SDK.
Override it with `-DPOMELO_DXC_EXECUTABLE=/absolute/path/to/dxc` (or `dxc.exe`
on Windows). The Linux installation uses `~/.local/bin/pomelo-dxc`. DXC is needed
only at build time. Generated files use separate directories per configuration.

The embedding script can also be run independently:

```sh
python3 deployment/embed_spirv.py build/generated/Debug/cube_vert.spv /tmp/cube_vert.cpp --symbol cube_vert_spirv --header graphics/shaders.hpp
```

Left-click and drag in the viewport to orbit around the volume. The camera stays
where you leave it; there is no automatic rotation or panning.

## Simulation

The default domain has 24³ cell-centered samples, with density increasing linearly from 0 to 1 along x.
Density uses two R32 float 3D images; the fixed velocity uses one RGBA32 float 3D
image. Velocity points along x, with speed increasing linearly from 0 at the first
y sample to 0.5 world units per second at the last. This divergence-free shear
moves each y slice at a different speed. Both ramps span sample centers; a
singleton axis has a zero ramp. Periodic boundaries wrap the density ramp from
its high end back to its low end. The velocity ramp also has a periodic seam
across y, with no flow through that seam.

Each submitted frame advances one 1/60-second simulation step using backward
semi-Lagrangian tracing and periodic trilinear interpolation, then renders the
new density. Simulation speed therefore depends on frame rate. Minimized windows
pause stepping, and viewport resizing preserves the simulation images.

The default density shears over time. Density does not influence velocity. Buoyancy,
pressure projection, velocity evolution, and density sources are not implemented.
Initial conditions and speed are defined in `src/graphics/initial_conditions.*`.

Run the numerical and GPU integration tests with:

```sh
ctest --test-dir build --output-on-failure
```

The advection test uses a graphics/compute Vulkan device (a software Vulkan driver
also works) and automatically enables synchronization validation when the Khronos
validation layer is available. It checks uniform preservation, periodic transport,
CPU/GPU agreement, immutable velocity, renderer image selection, and two frames in
flight with offscreen target resizing. GPU readback is restricted to tests.

## Source layout

Project code builds as three libraries:

- `application`: application loop, frame sequencing, SDL window/event ownership,
  and the Dear ImGui SDL adapter.
- `chrome`: Dear ImGui context, composable panels, and UI interaction state.
- `graphics`: Vulkan setup, resource allocation, swapchain presentation, simulation
  and rendering command recording, and the Dear ImGui Vulkan adapter.

Within `src/graphics`, `advection.*` handles simulation, `scene_renderer.*` handles
volume rendering, `image.*` provides shared image helpers, and `vulkan_backend.*`
owns the Vulkan object graph and submission. `initial_conditions.*` and
`uniform_grid.*` hold field initialization and grid data utilities. These are
focused source files within one library, not separate build targets.

`imgui_core` and SDL remain third-party dependencies. `shaders/` contains HLSL
sources and shared sampling helpers; `deployment/` contains build helpers.
