#include "geometry/uniform_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
void validate_domain(const geometry::UniformGrid& grid) {
    if (!std::isfinite(grid.spacing) || grid.spacing <= 0)
        throw std::invalid_argument("Grid spacing must be finite and positive");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double end = grid.origin[axis] + double(grid.spacing) * grid.dimensions[axis];
        if (!std::isfinite(grid.origin[axis]) || !std::isfinite(end) ||
            std::abs(end) > std::numeric_limits<float>::max())
            throw std::invalid_argument("Grid domain must be finite and representable as floats");
    }
}
}

std::size_t geometry::checked_cell_count(std::array<std::uint32_t, 3> dimensions) {
    std::size_t count = 1;
    for (auto dimension : dimensions) {
        if (!dimension) throw std::invalid_argument("Grid dimensions must be positive");
        if (count > std::numeric_limits<std::size_t>::max() / dimension)
            throw std::overflow_error("Grid cell count overflow");
        count *= dimension;
    }
    if (count > std::vector<float>().max_size()) throw std::overflow_error("Grid allocation size overflow");
    return count;
}

std::size_t geometry::cell_index(const UniformGrid& grid, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return x + std::size_t(grid.dimensions[0]) * (y + std::size_t(grid.dimensions[1]) * z);
}

std::array<double, 3> geometry::cell_center(const UniformGrid& grid, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return {grid.origin[0] + (x + 0.5) * grid.spacing,
            grid.origin[1] + (y + 0.5) * grid.spacing,
            grid.origin[2] + (z + 0.5) * grid.spacing};
}

float geometry::sinusoidal_value(double u, double v, double w) {
    return static_cast<float>(std::clamp(0.5 + 0.5 * std::sin(4 * std::numbers::pi * (u + v + w)), 0.0, 1.0));
}

void geometry::validate_grid(const UniformGrid& grid) {
    if (checked_cell_count(grid.dimensions) != grid.values.size())
        throw std::invalid_argument("Grid value count does not match dimensions");
    validate_domain(grid);
    for (float value : grid.values)
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::invalid_argument("Grid values must be finite and in [0,1]");
}

geometry::UniformGrid geometry::generate_sinusoidal_grid(std::array<std::uint32_t, 3> dimensions,
                                                       std::array<float, 3> origin, float spacing) {
    UniformGrid grid{dimensions, origin, spacing, {}};
    const auto count = checked_cell_count(dimensions);
    validate_domain(grid);
    grid.values.resize(count);
    for (std::uint32_t z = 0; z < dimensions[2]; ++z)
        for (std::uint32_t y = 0; y < dimensions[1]; ++y)
            for (std::uint32_t x = 0; x < dimensions[0]; ++x)
                grid.values[cell_index(grid, x, y, z)] = sinusoidal_value(
                    (x + 0.5) / dimensions[0], (y + 0.5) / dimensions[1], (z + 0.5) / dimensions[2]);
    return grid;
}
