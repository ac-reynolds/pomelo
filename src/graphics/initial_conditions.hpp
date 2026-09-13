#pragma once
#include "graphics/uniform_grid.hpp"
#include <array>
#include <vector>

namespace simulation {
struct InitialConditions {
    geometry::UniformGrid density;
    // xyz are world units per simulated second; w is reserved and zero.
    std::vector<std::array<float, 4>> velocity;
};
// Density ramps from 0 to 1 along x; x velocity ramps from 0 to speed along y.
InitialConditions initial_conditions(std::array<uint32_t, 3> dimensions = {24, 24, 24},
    std::array<float, 3> origin = {-1, -1, -1}, float spacing = 2.0f / 24, float speed = 0.5f);
void validate_initial_conditions(const InitialConditions&);
}
