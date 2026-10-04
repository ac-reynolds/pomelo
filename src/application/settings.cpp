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
