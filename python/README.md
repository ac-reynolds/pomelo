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
the fixed 64³ domain spanning [-1, 1] on each axis. Other simulator parameters
retain their native defaults.

Reading `settings.sources` returns a copy. To edit sources, assign a replacement:

```python
sources = settings.sources
sources.append((0.0, -0.85, 0.0))
settings.sources = sources
```

Numeric conversion errors raise `TypeError` or `ValueError` at construction or
assignment. `run` checks opacity and emitter bounds before starting graphics and
raises `ValueError` for invalid settings. Errors identify fields such as
`sources[1].y`. Booleans, strings, non-finite numbers, float32 overflow, and
malformed coordinate triples are rejected. Native startup/execution failures
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
