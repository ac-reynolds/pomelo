# Pomelo Python API

From the repository root:

```sh
python -m pip install ./python
python python/examples/twin_plumes.py
python -m unittest discover -s python/tests
```

Building requires C++20, CMake, Vulkan development support, SPIR-V-capable DXC,
and the native SDL build prerequisites. SDL is fetched by CMake. This initial
package supports local source installation on Linux and interpreter-specific
CPython wheels; it does not promise portable binary wheels. A Vulkan driver and
desktop display are required to run. Importing initializes no graphics resources.

```python
import pomelo

settings = pomelo.Settings(
    domain_box_opacity=0.45,
    sources=[(-0.35, -0.85, 0.0), (0.35, -0.85, 0.0)],
)
pomelo.run(settings)
# Execution continues after the window closes.
```

`Settings` takes keyword arguments only. `default_settings()` returns fresh
simulator defaults: grid-line opacity 0.65 and a single built-in emitter.
Omitted sources or `None` selects that emitter; `[]` disables injection. Opacity
ranges from zero to one and is independent of smoke opacity. Sources are finite
world-space triples with positive Y upward, and emitters must fit entirely in
the fixed 64³ domain spanning [-1, 1] on each axis.

The classic-flow settings are available through the same API:

```python
settings = pomelo.Settings(
    preset="taylor_green",
    sources=[],
    thermal_buoyancy=False,
    density_dissipation=0,
    pressure_iterations=512,
    domain_box_opacity=0.06,
    camera_yaw=0.55,
    camera_pitch=-0.3,
    camera_zoom=1.0,
)
pomelo.run(settings)
```

`preset` is `"smoke"` (default), `"taylor_green"`, `"double_shear_layer"`, or
`"vortex_ring"`. Choosing a preset changes only initial fields; it does not
implicitly replace sources or other settings. `thermal_buoyancy` is a strict
Python bool, defaulting to `True`; `False` disables heat injection and buoyancy.
`density_dissipation` is an independent rate in simulated seconds⁻¹, from 0 to
10 (default 0.2). Passive-dye demos set it to zero and set `sources=[]`.

`camera_yaw` and `camera_pitch` are radians; defaults are 0 and -0.35. Pitch is
limited to [-1.5, 1.5]. `camera_zoom` ranges from 0.5 to 3 (default 1), with
larger values moving closer. These are initial presentation settings, not
simulation parameters. All are writable properties and included in `repr`.
`pressure_iterations` is an integer from 1 to 4096 (default 64); the showcase
decks use 512 to improve the existing Jacobi pressure solve at additional GPU
cost. Booleans and floats are rejected, even if a float has an integer value.
Increasing this budget reduces pressure residuals but does not establish solver
convergence. The equations and boundary conditions are unchanged.
The dye represents a tracer in a filled, single-phase domain; no free-surface
liquid model is implemented. See [the showcase notes](../docs/cfd_showcase.md).

Reading `settings.sources` returns a copy. To edit sources, assign a replacement:

```python
sources = settings.sources
sources.append((0.0, -0.85, 0.0))
settings.sources = sources
```

Numeric conversion errors raise `TypeError` or `ValueError` at construction or
assignment. `run` checks numeric ranges and emitter bounds before starting graphics and
raises `ValueError` for invalid settings. Errors identify fields such as
`sources[1].y`. Numeric fields reject booleans, strings, non-finite numbers,
and float32 overflow; malformed coordinate triples are rejected. Unknown presets
raise `ValueError`, and `thermal_buoyancy` rejects non-bool values. Native startup/execution failures
raise `RuntimeError`; normal window closure returns `None`.

Call `run` on the Python main thread in a normal script. It blocks while releasing
the GIL so other Python threads can progress. It snapshots settings, so edits do
not affect an active run. Ctrl-C raises `KeyboardInterrupt` after cleanup,
including when minimized. Sequential runs create fresh windows and simulations;
overlapping or reentrant runs fail. Notebook and existing GUI event-loop
integration have not been validated; shared SDL ownership is unsupported.

Python bindings, package code, tests, and examples live in `python/`. The root
C++ build remains independent, with `POMELO_BUILD_PYTHON` defaulting to OFF.

Desktop integration tests are opt-in and run bounded subprocesses:

```sh
POMELO_TEST_GUI=1 python -m unittest discover -s python/tests
```

They check interruption, fresh sequential runs, settings snapshots, and Python
thread progress. Manually check window closure, orbit controls, resizing, and
Ctrl-C while minimized.
