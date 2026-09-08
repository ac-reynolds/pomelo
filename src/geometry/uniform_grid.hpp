#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geometry {

struct UniformGrid {
    std::array<std::uint32_t, 3> dimensions{};
    std::array<float, 3> origin{};
    float spacing{};
    std::vector<float> values;
};

// X is the fastest-changing coordinate. Callers provide in-range cell indices.
std::size_t cell_index(const UniformGrid& grid, std::uint32_t x, std::uint32_t y, std::uint32_t z);
std::array<double, 3> cell_center(const UniformGrid& grid, std::uint32_t x, std::uint32_t y, std::uint32_t z);
float sinusoidal_value(double u, double v, double w);
std::size_t checked_cell_count(std::array<std::uint32_t, 3> dimensions);
void validate_grid(const UniformGrid& grid);
UniformGrid generate_sinusoidal_grid(std::array<std::uint32_t, 3> dimensions,
                                    std::array<float, 3> origin, float spacing);

} // namespace geometry
