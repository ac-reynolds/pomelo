# Pomelo

A C++20 smoke simulator using SDL3 and Vulkan. Compute shaders evolve density, temperature, and velocity, and a full-window ray marcher renders the smoke density.

## Prerequisites

- CMake 3.24+
- A C++20 compiler
- Python 3 (for shader embedding during the build)
- Vulkan headers, loader, and a working Vulkan driver
- SPIR-V-enabled DXC (tested with v1.9.2607 on Linux)
- Git (SDL3 are fetched during the first configure)

On Ubuntu/Debian, the Vulkan development dependency is typically `libvulkan-dev`. SDL3 do not need to be installed system-wide.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/pomelo
```

The first configure needs internet access to fetch the pinned SDL3 dependency.
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

The default domain has 64³ cell-centered samples, initially at rest with no smoke.
A source near the bottom injects density and heat. Each submitted frame advances
one 1/60-second step: semi-Lagrangian advection, smoke injection and buoyancy,
pressure solving, and velocity projection. The renderer displays the resulting density.
Simulation speed depends on frame rate. Minimized windows pause stepping;
window resizing preserves the simulation fields and camera.

Initial conditions are defined in `src/graphics/initial_conditions.*`, and emitter
and buoyancy parameters in `src/graphics/smoke.*`.

Run the numerical and GPU integration tests with:

```sh
ctest --test-dir build --output-on-failure
```

The advection test uses a graphics/compute Vulkan device (a software Vulkan driver
also works) and automatically enables synchronization validation when the Khronos
validation layer is available. It checks uniform preservation, periodic transport,
CPU/GPU agreement, velocity self-advection, renderer image selection, and two frames in
flight with offscreen target resizing. GPU readback is restricted to tests.

## Source layout

Project code builds as two libraries:

- `application`: application loop, frame sequencing, and SDL window/event ownership.
- `graphics`: Vulkan setup, resource allocation, swapchain presentation, simulation
  and rendering command recording.

Within `src/graphics`, `fields.*` owns simulation images and their current versions,
`advection.*` records operations against borrowed fields, `volume_view.*`
binds each volume view to one scalar field at creation, sharing pipeline state
between views. Frame composition selects the view for the latest output; the
recorder takes only that view, a target, and a camera. `render_target.*` owns offscreen output
attachments. `field_view.hpp` describes borrowed image versions and their domain.
`image.*` provides shared image helpers, and `vulkan_backend.*`
owns the Vulkan object graph and submission. `initial_conditions.*` and
`uniform_grid.*` hold field initialization and grid data utilities. These are
focused source files within one library, not separate build targets.

SDL3 is fetched as a third-party dependency. `shaders/` contains HLSL
sources and shared sampling helpers; `deployment/` contains build helpers.

The R32 float divergence image is recomputed with periodic central differences
before the pressure solve and after projection as a diagnostic of the final velocity.
