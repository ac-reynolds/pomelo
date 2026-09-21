#include "graphics/initial_conditions.hpp"
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
