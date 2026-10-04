#pragma once
#include "graphics/field_view.hpp"
#include <array>
#include <vector>

namespace application {
struct Settings {
    float domain_box_opacity = 0.65f;
    std::vector<std::array<float, 3>> sources;
};
Settings default_settings();
Settings default_settings(const graphics::FieldDomain&);
void validate_settings(const Settings&, const graphics::FieldDomain&);
}
