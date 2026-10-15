#pragma once
#include "graphics/field_view.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace simulation { struct InitialConditions; struct SmokeParameters; }

namespace application {
struct Settings {
    float domain_box_opacity = 0.65f;
    std::vector<std::array<float, 3>> sources;
    std::string preset = "smoke";
    bool thermal_buoyancy = true;
    float density_dissipation = 0.2f; // Per simulated second; zero for passive dye.
    float camera_yaw = 0.0f; // Radians, matching the interactive orbit camera.
    float camera_pitch = -0.35f;
    float camera_zoom = 1.0f; // Presentation-only distance divisor.
    uint32_t pressure_iterations = 64; // Existing Jacobi budget; no equation changes.
};
Settings default_settings();
Settings default_settings(const graphics::FieldDomain&);
void validate_settings(const Settings&, const graphics::FieldDomain&);
// Shared by the interactive application and deterministic offscreen captures.
simulation::InitialConditions make_initial_conditions(const Settings&, const graphics::FieldDomain&);
simulation::SmokeParameters make_smoke_parameters(const Settings&, const graphics::FieldDomain&);
}
