#include "application/settings.hpp"
#include "graphics/smoke.hpp"
#include "graphics/initial_conditions.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

application::Settings application::default_settings() {
    return default_settings(simulation::smoke_domain());
}

application::Settings application::default_settings(const graphics::FieldDomain& domain) {
    Settings result;
    result.sources = {simulation::smoke_parameters(domain).center};
    return result;
}

void application::validate_settings(const Settings& deck, const graphics::FieldDomain& domain) {
    if (!std::isfinite(deck.domain_box_opacity) || deck.domain_box_opacity < 0 || deck.domain_box_opacity > 1)
        throw std::invalid_argument("domain_box_opacity: must be a finite number between 0 and 1");
    if (deck.preset != "smoke" && deck.preset != "taylor_green" && deck.preset != "double_shear_layer" && deck.preset != "vortex_ring")
        throw std::invalid_argument("preset: expected smoke, taylor_green, double_shear_layer, or vortex_ring");
    if (!std::isfinite(deck.density_dissipation) || deck.density_dissipation < 0 || deck.density_dissipation > 10)
        throw std::invalid_argument("density_dissipation: must be a finite number between 0 and 10");
    if (!std::isfinite(deck.camera_yaw))
        throw std::invalid_argument("camera_yaw: must be finite");
    if (!std::isfinite(deck.camera_pitch) || deck.camera_pitch < -1.5f || deck.camera_pitch > 1.5f)
        throw std::invalid_argument("camera_pitch: must be a finite number between -1.5 and 1.5 radians");
    if (!std::isfinite(deck.camera_zoom) || deck.camera_zoom < 0.5f || deck.camera_zoom > 3.0f)
        throw std::invalid_argument("camera_zoom: must be a finite number between 0.5 and 3");
    if (deck.pressure_iterations < 1 || deck.pressure_iterations > 4096)
        throw std::invalid_argument("pressure_iterations: must be an integer between 1 and 4096");
    const double radius = simulation::smoke_parameters(domain).radius;
    for (size_t i = 0; i < deck.sources.size(); ++i) {
        for (size_t axis = 0; axis < 3; ++axis) {
            const double coordinate = deck.sources[i][axis];
            const double minimum = domain.origin[axis], maximum = minimum + double(domain.dimensions[axis]) * domain.spacing;
            const std::string path = "sources[" + std::to_string(i) + "]." + "xyz"[axis];
            if (!std::isfinite(coordinate)) throw std::invalid_argument(path + ": coordinate must be finite");
            if (coordinate - radius < minimum || coordinate + radius > maximum)
                throw std::invalid_argument(path + ": emitter extends beyond the domain");
        }
    }
}

simulation::InitialConditions application::make_initial_conditions(const Settings& settings,
    const graphics::FieldDomain& domain) {
    validate_settings(settings, domain);
    if (settings.preset == "smoke")
        return simulation::smoke_initial_conditions(domain.dimensions, domain.origin, domain.spacing);
    const auto preset = settings.preset == "taylor_green" ? simulation::DemoPreset::taylor_green
        : settings.preset == "double_shear_layer" ? simulation::DemoPreset::double_shear_layer
        : simulation::DemoPreset::vortex_ring;
    return simulation::demo_initial_conditions(preset, domain);
}

simulation::SmokeParameters application::make_smoke_parameters(const Settings& settings,
    const graphics::FieldDomain& domain) {
    validate_settings(settings, domain);
    auto result = simulation::smoke_parameters(domain);
    result.dissipation = settings.density_dissipation;
    if (!settings.thermal_buoyancy) {
        result.heat_rate = 0;
        result.buoyancy = 0;
    }
    return result;
}
