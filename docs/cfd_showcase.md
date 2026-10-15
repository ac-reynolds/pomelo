# Periodic CFD showcase

These examples use Pomelo's native Vulkan simulation and volume renderer. They
are single-phase, incompressible-flow demonstrations with an advected scalar
that makes the motion visible. In the nonthermal examples, the field called
`density` is dye concentration, not a varying material density or a liquid
surface. Turning off thermal buoyancy makes dyed-fluid motion possible without
implementing a free-surface solver.

## Why these examples

| Candidate | Fit to the current solver | Decision |
| --- | --- | --- |
| Localized vortex ring | A resolved ring and its induced velocity can be initialized without solid boundaries or a new force solver | Strong, recognizable three-dimensional example |
| Double shear layer / Kelvin–Helmholtz-style roll-up | Smooth periodic initial velocity; no walls, continuous forcing, or obstacles needed | Clear dye-stretching and mixing example |
| Taylor–Green vortex | Familiar three-dimensional periodic initial-value problem | Canonical comparison, with dye making deformation visible |
| Opposed jets | Localized velocity impulses or sustained directional momentum sources are feasible, but need additional forcing controls | Defer; unlike two buoyant plumes, opposed jets must inject opposing momentum |
| Lid-driven cavity | Requires moving-lid and no-slip wall conditions, a boundary-aware pressure solve, and explicit viscosity for a prescribed Reynolds number | Outside this minimal-change showcase |
| Flow past a cylinder | Requires an actual impermeable obstacle and its pressure/velocity treatment, plus meaningful inflow/outflow or carefully designed periodic forcing | Outside this minimal-change showcase |

The [DFG cylinder benchmark](https://wwwold.mathematik.tu-dortmund.de/~featflow/en/benchmarks/cfdbenchmarking/flow.html)
is a specific boundary-value problem. A cylinder drawn in a periodic volume
would not reproduce it. Similarly, the
[lid-driven cavity](https://ansyshelp.ansys.com/public/Views/Secured/corp/v252/en/dem_vm/lid-driven-cavity.html)
depends on no-slip stationary walls and a moving lid.

The pre-existing `inputs/ring.json` places six smoke emitters on a circle. It is
not a vortex-ring initial condition.

## Common numerical setup

- Default domain: `[-1, 1]^3`, with `64^3` samples and spacing `h = 2/64`
- Fixed simulation step: `dt = 1/60` simulated seconds per submitted step
- Semi-Lagrangian trilinear advection of velocity, dye, and temperature
- Periodic boundary conditions for sampling and pressure/projection
- Zero initial temperature for the nonthermal demonstrations
- No ongoing injection, thermal acceleration, or intentional dye dissipation
  in the passive-dye example decks
- Existing density transfer function and native ray marcher
- Existing weighted-Jacobi pressure solver, with a configurable iteration budget;
  legacy default 64, showcase decks 512

The new settings are optional, so existing smoke decks remain valid. The
passive-dye decks use `sources: []`, `thermal_buoyancy: false`, and
`density_dissipation: 0`. Preset names are `vortex_ring`, `double_shear_layer`,
and `taylor_green`; `smoke` retains the original initialization. Camera angles
are `camera_yaw` and `camera_pitch`, in radians; `camera_zoom` scales the fitted
view for clearer framing without altering simulation fields.

`thermal_buoyancy: false` disables heat injection and buoyant acceleration; it
does not disable density injection from a nonempty source list. Choosing a
non-smoke preset does not silently change the other settings. Use the explicit
passive-dye settings above for unforced evolution. The default settings remain
thermal buoyancy enabled, dye dissipation `0.2` per simulated second, yaw `0`,
and pitch `-0.35`, zoom `1`, and pressure budget `64`.

The actual projection implementation uses a backward-difference divergence and
forward-difference pressure gradient. Their composition matches the
nearest-neighbor seven-point pressure Laplacian. Earlier README wording that
described central differences was stale and is corrected on this branch. The showcase does not replace this
projection, pressure solve, or advection mathematics.

For an array `f`, define the periodic backward derivative

```text
D_j^- f[i] = (f[i] - f[i-e_j]) / h
```

Initializing velocity as the matching discrete curl of a vector potential
ensures `D^- · velocity = 0` to floating-point roundoff: the mixed difference
operators commute. This avoids imposing a large pressure-correction transient
just to start a demo.

## Taylor–Green vortex

The classical continuum initial velocity, with normalized coordinates
`q = (position + 1)/2`, uses `U = 0.5` world units per simulated second:

```text
u =  U sin(2πqx) cos(2πqy) cos(2πqz)
v = -U cos(2πqx) sin(2πqy) cos(2πqz)
w = 0
```

The [FluidSim implementation](https://fluidsim.readthedocs.io/en/latest/ipynb/executed/taylor-green.html)
provides a primary, executable reference for this periodic three-dimensional
initial-value problem. Its specified-viscosity simulation is not a reference
solution for Pomelo's numerically dissipative flow.

For discrete compatibility, initialize the vector potential

```text
Ax = Ay = 0
Az = (U/π) sin(2πqx) sin(2πqy) cos(2πqz)
velocity = curl^- A
```

This is a finite-difference approximation of the continuum Taylor–Green field.
The seeded dye sheets use `G(d,s) = exp(-d²/(2s²))`:

```text
c = [G(qy-0.25,0.045) + G(qy-0.75,0.045)]
    [0.55 + 0.45 cos²(2πqz)]
```

They are visualization tracers and are evolved by the same
advection step as the other fields; their later positions are not animated by
an analytic formula. The fully three-dimensional evolved flow should not be
confused with the simpler two-dimensional Taylor–Green exact decay solution.

## Double shear layer

An opposing pair of broad shear layers fits the periodic domain naturally:

```text
u = U tanh(30(qy-0.25))       for qy <= 0.5
u = U tanh(30(0.75-qy))       for qy > 0.5
v = ε U sin(2πqx)
w = 0
```

The implementation uses `U = 0.5` and `ε = 0.05`; the characteristic shear
width `1/30` is about `2.1` cells in normalized coordinates. Dye is

```text
c = [G(qy-0.25,0.045) + G(qy-0.75,0.045)] G(qz-0.5,0.12)
```

Here `u` depends only on `y` and `v` only on `x`, so the initial velocity is
discretely divergence-free even without a projection. The transverse sine
perturbation seeds roll-up; it is an initial condition, not a continuing force.
The dye marks the interfaces, with limited depth to keep a volume rendering
readable. The velocity is initially independent of `z`, so this is an extruded
two-dimensional shear-flow demonstration running in the three-dimensional
solver, not evidence of resolved three-dimensional turbulence.

The tanh-profile family appears in
[COMSOL's double-shear-layer example](https://www.comsol.it/blogs?p=28603).
The smooth periodic shear profiles and passive-dye interpretation are also
motivated by [Lecoanet et al., A validated non-linear Kelvin–Helmholtz benchmark for numerical hydrodynamics](https://academic.oup.com/mnras/article/455/4/4274/1267403).
That work emphasizes smooth initial data, explicit diffusion, and convergence
when making benchmark claims. These showcase parameters and numerics differ;
small wisps alone are not evidence of physical accuracy.

## Localized vortex ring

The ring is initialized with a regularized circular-filament vector potential,
then a compatible discrete curl. The default-box constants are `R = 0.35`,
`y0 = -0.25`, `a = 0.10`, `Γ = 0.45`, and `M = 96`. For `M` azimuthal samples,

```text
θk  = 2π(k+0.5)/M
Xk  = (R cos θk, y0, R sin θk)
dlk = (R sin θk, 0, -R cos θk) (2π/M)
A(p) = Γ/(4π) sum_k dlk / sqrt(|p-Xk|² + a²)
```

The orientation gives positive-`y` self-induced motion. The core radius `a`
regularizes the singular filament. Velocity is constructed as `curl^- A`, not
assigned a uniform translation. After initialization, Pomelo evolves the
velocity and dye normally; the ring is not moved along a prescribed path.

To make the potential smooth across the finite periodic box, multiply it by

```text
W(p) = product_j [1 - smoothstep(0.70, 0.95, |pj|)]
```

before taking the discrete curl. This window preserves the central ring region
and introduces compensating outer vorticity. Consequently this is a localized
periodic ring demonstration, not an isolated unbounded-domain vortex-ring
benchmark. A toroidal Gaussian dye distribution reveals the core:

```text
c = exp(-[(sqrt(x²+z²)-R)² + (y-y0)²] / (2σ²))
```

The dye width is `σ = 0.12` world units, about `3.8` cells; the regularization
radius is `3.2` cells. These are deliberately broad enough to survive the
initial interpolation better than a subcell filament.

The physical motivation is the regularized Biot–Savart/vector-potential approach
described in [Winckelmans, Topics in vortex methods for the computation of three- and two-dimensional incompressible unsteady flows](https://thesis.library.caltech.edu/4385/5/winckelmans-gs_1989.pdf).
The particular taper, dye, discretization, and parameters here are showcase
design choices. An alternative is an impulsive, localized axial body force,
as studied by [Sonmez et al., Generation of laminar vortex rings by an impulsive body force](https://arxiv.org/abs/2304.02425),
but that would require a new source-force control path.

## Why expose the pressure budget?

The new `pressure_iterations` field routes a deck value to the existing
`PressureSettings.iterations`. It does not change the pressure stencil,
relaxation factor, tolerances, normalization, or projection shader.

The requirement is specifically to reduce residual divergence in these evolving
vortex/shear demonstrations. In a 64³ Taylor–Green comparison after 30 submitted
steps, the pressure residual/RHS RMS ratio was approximately 0.733 with 64
iterations, 0.295 with 256, and 0.0921 with 512. These are separate full
trajectories, not a fixed-right-hand-side convergence experiment: changing the
projection also changes later velocity and pressure right-hand sides.

The example decks therefore select 512. Legacy decks still use 64. More
iterations cost more time; 512 still did not meet the existing relative tolerance
in that comparison. This is an exposed accuracy/performance control, not a claim
that the pressure solve is now converged. Decrease it explicitly for a faster
preview; output will differ from the committed showcase captures.

## Interpretation and limits

- These are genuine numerical flow demonstrations, not validated benchmark
  reproductions. No physical Reynolds number, convergence result, or
  turbulence-resolution claim is attached to them
- There is no explicit physical viscosity in the present stepping scheme.
  Semi-Lagrangian interpolation supplies numerical diffusion, whose effect
  depends on grid resolution, velocity, and time step
- Zero configured dye dissipation removes the intentional exponential decay;
  it does not remove interpolation diffusion or make dye mass exactly conserved
- Thin cores and filaments are only a few cells wide at `64^3`; they will soften
  and may disappear as concentration drops below the renderer's opacity range
- The periodic box is not a sealed tank. Fluid and dye can wrap through the
  faces, and the box grid is a visual reference, not a solid boundary
- The pressure solver's finite iteration budget leaves residual divergence.
  A visually convincing sequence does not replace residual and convergence checks
- The passive dye represents neither a liquid surface nor a second fluid.
  There is no surface tension, splashing, liquid-air interface, or obstacle flow
- A captured frame must be identified by the actual preset, simulation step,
  camera, and native renderer. Initial-condition previews alone do not establish
  that time evolution was captured

## Implementation traceability

Initialization belongs in `src/graphics/initial_conditions.*`; application
selection and physical-mode settings belong in `src/application/settings.*`.
Example decks select these settings without rewriting the solver shaders.
The optional camera zoom and lower box-grid opacity are presentation changes
needed to keep the smaller ring and evolving dye readable. Increasing the ring
dye width to 0.12 affects only the initial tracer, not its velocity field.
Native stepping continues through the existing `VulkanBackend`, and rendering
continues through the existing volume-view/ray-march path.

For reproducibility, record the source revision, complete input deck,
simulation step count, output size, camera angles, and Vulkan device alongside
each capture. A software Vulkan implementation still executes the native
compute/render path; it should be identified as software rendering rather than
presented as discrete-GPU performance.
