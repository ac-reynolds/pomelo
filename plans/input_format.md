# Input deck format

Run `./build/pomelo path/to/deck.json`. The deck is loaded and validated before
creating a window or allocating GPU resources. With no filename, the original
single-emitter smoke configuration is used.

## Schema

The root must be a JSON object. Fields may appear in any order. The original
`domain_box_opacity` and `sources` fields remain required; all additions below
are optional and preserve existing defaults.

| Field | Type | Default / requirement | Meaning |
| --- | --- | --- | --- |
| `domain_box_opacity` | number | Required, [0, 1] | Opacity of the domain grid, independent of dye opacity |
| `sources` | array | Required | Zero or more emitter-center objects with exactly `x`, `y`, and `z` numbers |
| `preset` | string | `"smoke"` | `"smoke"`, `"taylor_green"`, `"double_shear_layer"`, or `"vortex_ring"` initial fields |
| `thermal_buoyancy` | boolean | `true` | If false, heat injection and thermal acceleration are zero |
| `density_dissipation` | number | 0.2, [0, 10] | Explicit scalar decay rate per simulated second |
| `camera_yaw` | number | 0 | Initial orbit yaw in radians |
| `camera_pitch` | number | -0.35, [-1.5, 1.5] | Initial orbit pitch in radians |
| `camera_zoom` | number | 1, [0.5, 3] | Initial distance divisor; larger values move closer |
| `pressure_iterations` | integer | 64, [1, 4096] | Number of iterations in the existing Jacobi pressure solve |

All numbers must be finite and representable as float32. JSON booleans are not
numbers. Comments, trailing commas, duplicate keys (including escaped aliases),
unknown fields, incomplete source objects, and extra content after the object
are rejected with a field-specific error. Enum strings are case-sensitive.
`pressure_iterations` must use a JSON integer token, without a decimal point or
exponent; booleans and strings are rejected.

## Simulation semantics

The domain remains 64³ cell-centered samples spanning [-1, 1] on each axis,
periodic in every direction. Positive Y is upward. Emitter centers are world
coordinates and the entire built-in emitter must fit inside the domain.
An empty `sources` array disables continuous injection. Multiple sources add
density and heat; cooling, dissipation, and buoyancy apply once per step.

`preset` only selects initial velocity and tracer fields. It does not change
any other setting, hide sources, or select a different solver. `"smoke"`
initializes zero velocity, density, and excess temperature. Each classic-flow
preset initializes zero excess temperature plus a deterministic velocity and
dye pattern. The Taylor–Green and vortex-ring velocities are discrete curls
using the solver's backward-difference divergence convention; double shear is
also discretely divergence-free by construction. Exact definitions and limits
are in [the showcase notes](../docs/cfd_showcase.md).

`thermal_buoyancy: false` sets both heat injection and the buoyancy coefficient
to zero. It does **not** disable scalar injection, density dissipation, or
advection. The independent scalar decay multiplies density by
`exp(-density_dissipation * dt)` before sources are added. Setting its rate to
zero removes this explicit decay, but numerical diffusion from interpolation
remains. Cooling retains its original coefficient; with no initial heat and
no heat injection, temperature remains zero.

A non-buoyant passive-dye deck therefore uses all three settings:

```json
{
  "preset": "double_shear_layer",
  "thermal_buoyancy": false,
  "density_dissipation": 0,
  "pressure_iterations": 512,
  "sources": [],
  "domain_box_opacity": 0.035,
  "camera_yaw": 0,
  "camera_pitch": 0,
  "camera_zoom": 1
}
```

Here density is a visualization tracer in an already-filled, single-phase
fluid. It is not a liquid volume fraction or an air/liquid interface. There
are no walls, free surfaces, splashes, or surface tension. No explicit physical
viscosity is exposed, so these are qualitative flow demos, not calibrated
Reynolds-number benchmarks.

The three showcase decks set `pressure_iterations: 512` because the legacy
64-iteration budget leaves substantial low-frequency pressure residuals in
these initialized flows. This is a traceable configuration change to the
iteration budget only: pressure equations, divergence/gradient stencils,
advection, and boundary conditions are unchanged. More iterations cost GPU
time, and a fixed budget is not a convergence guarantee. Existing decks and
omitted settings keep 64 iterations.

## Camera and compatibility

Camera values affect rendering only. Mouse orbit begins at the configured
angles and retains the configured zoom. A zoom above one can crop the domain
box; this is useful for a localized vortex-ring capture. The default zoom one
retains the original fitted view.

All five original decks remain valid with unchanged smoke behavior. The same
settings are exposed by the Python `Settings` API. In Python only, omitted
`sources` or `sources=None` selects the built-in emitter, so explicitly pass
`sources=[]` for source-free flow demos. `run(settings)` snapshots and validates
the configuration before graphics startup.
