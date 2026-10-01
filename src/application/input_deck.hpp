#pragma once

#include "graphics/field_view.hpp"

#include <array>
#include <string_view>
#include <vector>

namespace application {

struct InputDeck {
    float domain_box_opacity = 0.65f;
    std::vector<std::array<float, 3>> sources;
};

InputDeck default_input_deck(const graphics::FieldDomain&);
InputDeck parse_input_deck(std::string_view);
InputDeck load_input_deck(std::string_view filename);
// Validate against the actual domain and default emitter radius, before GPU setup.
void validate_input_deck(const InputDeck&, const graphics::FieldDomain&);

} // namespace application
