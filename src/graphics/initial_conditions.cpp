#include "graphics/initial_conditions.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

void simulation::validate_initial_conditions(const InitialConditions& initial) {
    geometry::validate_grid(initial.density);
    if (initial.temperature.size() != initial.density.values.size())
        throw std::invalid_argument("Temperature count does not match density dimensions");
    for (float t : initial.temperature)
        if (!std::isfinite(t) || t < 0) throw std::invalid_argument("Temperature must be finite and nonnegative");
    if (initial.velocity.size() != initial.density.values.size())
        throw std::invalid_argument("Velocity count does not match density dimensions");
    for (const auto& v : initial.velocity)
        for (float component : v)
            if (!std::isfinite(component)) throw std::invalid_argument("Velocity must be finite");
}

simulation::InitialConditions simulation::initial_conditions(std::array<uint32_t, 3> dimensions,
    std::array<float, 3> origin, float spacing, float speed) {
    if (!std::isfinite(speed)) throw std::invalid_argument("Speed must be finite");
    InitialConditions result;
    result.density = {dimensions, origin, spacing,
                      std::vector<float>(geometry::checked_cell_count(dimensions), 0.0f)};
    geometry::validate_grid(result.density);
    result.velocity.resize(result.density.values.size());
    result.temperature.resize(result.density.values.size());
    // Density spans the sample centers. Crossed velocity components each make
    // one periodic cycle over the domain and initially have zero divergence.
    const float x_scale = dimensions[0] > 1 ? 1.0f / (dimensions[0] - 1) : 0.0f;
    constexpr double tau = 2 * std::numbers::pi;
    for (uint32_t z = 0; z < dimensions[2]; ++z)
        for (uint32_t y = 0; y < dimensions[1]; ++y)
            for (uint32_t x = 0; x < dimensions[0]; ++x) {
                const auto index = geometry::cell_index(result.density, x, y, z);
                result.density.values[index] = float(x) * x_scale;
                const double phase_x = tau * (x + 0.5) / dimensions[0];
                const double phase_y = tau * (y + 0.5) / dimensions[1];
                result.velocity[index] = {
                    dimensions[1] > 1 ? float(speed * std::sin(phase_y)) : 0.0f,
                    dimensions[0] > 1 ? float(speed * std::sin(phase_x)) : 0.0f, 0, 0};
            }
    validate_initial_conditions(result);
    return result;
}

simulation::InitialConditions simulation::smoke_initial_conditions(std::array<uint32_t, 3> dimensions,
    std::array<float, 3> origin, float spacing) {
    InitialConditions result;
    const auto count = geometry::checked_cell_count(dimensions);
    result.density = {dimensions, origin, spacing, std::vector<float>(count, 0)};
    result.temperature.resize(count);
    result.velocity.resize(count);
    validate_initial_conditions(result);
    return result;
}

simulation::InitialConditions simulation::demo_initial_conditions(DemoPreset preset,
    const graphics::FieldDomain& domain) {
    auto result = smoke_initial_conditions(domain.dimensions, domain.origin, domain.spacing);
    const auto n = domain.dimensions;
    constexpr double tau = 2 * std::numbers::pi;
    constexpr double speed = 0.5;
    std::vector<std::array<float, 3>> potential;
    if (preset != DemoPreset::double_shear_layer) potential.resize(result.velocity.size());
    constexpr unsigned ring_segments = 96;
    constexpr double ring_radius = 0.35, ring_y = -0.25, ring_core = 0.10, circulation = 0.45;
    std::array<std::array<double, 4>, ring_segments> ring{};
    if (preset == DemoPreset::vortex_ring)
        for (unsigned k = 0; k < ring_segments; ++k) {
            const double theta = tau * (k + 0.5) / ring_segments;
            ring[k] = {ring_radius * std::cos(theta), ring_radius * std::sin(theta),
                ring_radius * std::sin(theta) * tau / ring_segments,
                -ring_radius * std::cos(theta) * tau / ring_segments};
        }
    auto gaussian = [](double distance, double sigma) {
        return std::exp(-0.5 * distance * distance / (sigma * sigma));
    };
    auto window = [](double p) {
        const double t = std::clamp((std::abs(p) - 0.70) / 0.25, 0.0, 1.0);
        return 1 - t * t * (3 - 2 * t);
    };
    for (uint32_t z = 0; z < n[2]; ++z)
        for (uint32_t y = 0; y < n[1]; ++y)
            for (uint32_t x = 0; x < n[0]; ++x) {
                const auto i = geometry::cell_index(result.density, x, y, z);
                const double qx = (x + 0.5) / n[0], qy = (y + 0.5) / n[1], qz = (z + 0.5) / n[2];
                if (preset == DemoPreset::double_shear_layer) {
                    // Periodic double shear layer, rho=30, perturbation delta=.05.
                    // Each component is constant along its own derivative axis.
                    const double u = qy <= 0.5 ? std::tanh(30 * (qy - 0.25)) : std::tanh(30 * (0.75 - qy));
                    result.velocity[i] = {float(speed * u), float(0.05 * speed * std::sin(tau * qx)), 0, 0};
                    result.density.values[i] = float((gaussian(qy - 0.25, 0.045) + gaussian(qy - 0.75, 0.045))
                        * gaussian(qz - 0.5, 0.12));
                } else if (preset == DemoPreset::taylor_green) {
                    // A_z gives (U sin x cos y cos z, -U cos x sin y cos z, 0)
                    // in the continuum on a cube. The backward discrete curl
                    // below matches the solver's actual divergence stencil.
                    potential[i][2] = float(speed * n[1] * domain.spacing / tau
                        * std::sin(tau * qx) * std::sin(tau * qy) * std::cos(tau * qz));
                    result.density.values[i] = float((gaussian(qy - 0.25, 0.045) + gaussian(qy - 0.75, 0.045))
                        * (0.55 + 0.45 * std::pow(std::cos(tau * qz), 2)));
                } else if (preset == DemoPreset::vortex_ring) {
                    // Regularized circular-filament vector potential. Taper A
                    // (not velocity) to zero before periodic boundaries, keeping
                    // the resulting curl discretely divergence-free.
                    const double px = 2 * qx - 1, py = 2 * qy - 1, pz = 2 * qz - 1;
                    double ax = 0, az = 0;
                    for (const auto& segment : ring) {
                        const double dx = px - segment[0], dy = py - ring_y, dz = pz - segment[1];
                        const double inverse_distance = 1 / std::sqrt(dx * dx + dy * dy + dz * dz + ring_core * ring_core);
                        ax += segment[2] * inverse_distance;
                        az += segment[3] * inverse_distance;
                    }
                    const double scale = circulation / (2 * tau) * window(px) * window(py) * window(pz);
                    potential[i] = {float(ax * scale), 0, float(az * scale)};
                    const double radial = std::hypot(px, pz) - ring_radius;
                    result.density.values[i] = float(gaussian(std::hypot(radial, py - ring_y), 0.12));
                } else {
                    throw std::invalid_argument("Unknown demo preset");
                }
            }
    if (!potential.empty()) {
        const float inverse_spacing = 1 / domain.spacing;
        for (uint32_t z = 0; z < n[2]; ++z)
            for (uint32_t y = 0; y < n[1]; ++y)
                for (uint32_t x = 0; x < n[0]; ++x) {
                    const auto i = geometry::cell_index(result.density, x, y, z);
                    const auto xm = geometry::cell_index(result.density, (x + n[0] - 1) % n[0], y, z);
                    const auto ym = geometry::cell_index(result.density, x, (y + n[1] - 1) % n[1], z);
                    const auto zm = geometry::cell_index(result.density, x, y, (z + n[2] - 1) % n[2]);
                    const auto& a = potential[i];
                    result.velocity[i] = {
                        ((a[2] - potential[ym][2]) - (a[1] - potential[zm][1])) * inverse_spacing,
                        ((a[0] - potential[zm][0]) - (a[2] - potential[xm][2])) * inverse_spacing,
                        ((a[1] - potential[xm][1]) - (a[0] - potential[ym][0])) * inverse_spacing, 0};
                }
    }
    validate_initial_conditions(result);
    return result;
}
