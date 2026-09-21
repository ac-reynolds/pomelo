#pragma once
#include "graphics/uniform_grid.hpp"
#include <array>
#include <vector>

namespace simulation {
struct InitialConditions {
    geometry::UniformGrid density;
    // Temperature excess above ambient; zero is ambient.
    std::vector<float> temperature;
    // xyz are world units per simulated second; w is reserved and zero.
    std::vector<std::array<float, 4>> velocity;
};
// Density ramps from 0 to 1 along x. Velocity is speed*(sin(2*pi*qy), sin(2*pi*qx), 0),
// where q is normalized position in the domain. Speed is the component amplitude.
InitialConditions initial_conditions(std::array<uint32_t, 3> dimensions = {64, 64, 64},
    std::array<float, 3> origin = {-1, -1, -1}, float spacing = 2.0f / 64, float speed = 0.5f);
InitialConditions smoke_initial_conditions(std::array<uint32_t, 3> dimensions = {64, 64, 64},
    std::array<float, 3> origin = {-1, -1, -1}, float spacing = 2.0f / 64);
void validate_initial_conditions(const InitialConditions&);
}
