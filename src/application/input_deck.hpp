#pragma once

#include "application/settings.hpp"

#include <string_view>

namespace application {

using InputDeck = Settings;
inline InputDeck default_input_deck(const graphics::FieldDomain& domain) {
    return default_settings(domain);
}
InputDeck parse_input_deck(std::string_view);
InputDeck load_input_deck(std::string_view filename);
// Validate against the actual domain and default emitter radius, before GPU setup.
inline void validate_input_deck(const InputDeck& deck, const graphics::FieldDomain& domain) {
    validate_settings(deck, domain);
}

} // namespace application
