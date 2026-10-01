#include "application/input_deck.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void reject(std::string_view text, std::string_view field) {
    try { application::parse_input_deck(text); }
    catch (const std::invalid_argument& e) {
        require(std::string_view(e.what()).find(field) != std::string_view::npos,
                "Parser error did not identify the field");
        return;
    }
    throw std::runtime_error("Invalid JSON deck was accepted: " + std::string(text));
}

void reject_domain(const application::InputDeck& deck, const graphics::FieldDomain& domain,
                   std::string_view field) {
    try { application::validate_input_deck(deck, domain); }
    catch (const std::invalid_argument& e) {
        require(std::string_view(e.what()).find(field) != std::string_view::npos,
                "Validation error did not identify the field");
        return;
    }
    throw std::runtime_error("Invalid simulator settings were accepted");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected example inputs directory");
        const graphics::FieldDomain domain{{64, 64, 64}, {-1, -1, -1}, 2.0f / 64};
        const auto defaults = application::default_input_deck(domain);
        require(defaults.sources.size() == 1 && defaults.domain_box_opacity == 0.65f,
                "Default deck did not retain built-in settings");
        application::validate_input_deck(defaults, domain);
        const auto parsed = application::parse_input_deck(
            " \n{\"sources\":[{\"z\":-0.2,\"x\":4e-1,\"y\":-0.85}],\"domain_box_opacity\":6.5E-1}\t");
        require(parsed.sources.size() == 1 && parsed.sources[0] == std::array<float, 3>{0.4f, -0.85f, -0.2f},
                "Source coordinates or field order were lost");
        application::validate_input_deck(parsed, domain);
        const auto escaped = application::parse_input_deck(
            R"({"\u0064omain_box_opacity":1,"sources":[{"\u0078":0,"y":0,"z":0}]})");
        require(escaped.sources.size() == 1 && escaped.domain_box_opacity == 1, "Escaped JSON keys failed");
        const auto empty = application::parse_input_deck(R"({"sources":[],"domain_box_opacity":0})");
        require(empty.sources.empty() && empty.domain_box_opacity == 0, "Empty sources or zero opacity failed");

        reject("{}", "domain_box_opacity");
        reject(R"({"domain_box_opacity":0})", "sources");
        reject(R"({"domain_box_opacity":0,"sources":[],"extra":1})", "extra");
        reject(R"({"domain_box_opacity":0,"domain_box_opacity":1,"sources":[]})", "domain_box_opacity");
        reject(R"({"domain_box_opacity":0,"sources":[],"\u0073ources":[]})", "sources");
        reject(R"({"domain_box_opacity":0,"sources":[{"x":0,"y":0}]})", "sources[0].z");
        reject(R"({"domain_box_opacity":0,"sources":[{"x":0,"x":1,"y":0,"z":0}]})", "sources[0].x");
        reject(R"({"domain_box_opacity":0,"sources":[{"q":0,"y":0,"z":0}]})", "sources[0].q");
        reject(R"({"domain_box_opacity":0,"sources":[{"x":"0","y":0,"z":0}]})", "sources[0].x");
        reject(R"({"domain_box_opacity":0,"sources":{}})", "sources");
        reject(R"({"domain_box_opacity":0,"sources":[null]})", "sources[0]");
        reject(R"({"domain_box_opacity":0,"sources":[],})", "input");
        reject(R"({"domain_box_opacity":0,"sources":[{"x":0,"y":0,"z":0},]})", "sources[1]");
        reject(R"({"domain_box_opacity":0,"sources":[]} extra)", "input");
        reject(R"({"domain_box_opacity":0,"sources":[] /* comment */})", "input");
        reject(R"({"domain_box_opacity":0,"sources":[{"x":1e39,"y":0,"z":0}]})", "sources[0].x");
        for (const auto number : {"-0.1", "1.1", "true", "null", "\"0\"", "NaN", "Infinity", "1e999", "+1", ".5", "01", "1.", "1e"})
            reject("{\"domain_box_opacity\":" + std::string(number) + ",\"sources\":[]}", "domain_box_opacity");
        reject(R"({"\uD800":0,"sources":[]})", "input");
        reject(R"({"\uDC00":0,"sources":[]})", "input");
        reject(R"({"\q":0,"sources":[]})", "input");

        auto invalid = defaults;
        invalid.sources.push_back({0, -0.99f, 0});
        reject_domain(invalid, domain, "sources[1].y");
        invalid = defaults; invalid.sources[0][2] = std::numeric_limits<float>::infinity();
        reject_domain(invalid, domain, "sources[0].z");
        invalid = defaults; invalid.domain_box_opacity = std::numeric_limits<float>::quiet_NaN();
        reject_domain(invalid, domain, "domain_box_opacity");
        // Bounds must come from the supplied domain, not a hard-coded unit box.
        const graphics::FieldDomain shifted{{32, 64, 32}, {4, -3, 10}, 0.125f};
        application::validate_input_deck(application::default_input_deck(shifted), shifted);
        reject_domain(parsed, shifted, "sources[0].x");

        unsigned examples{};
        for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
            if (entry.path().extension() != ".json") continue;
            const auto deck = application::load_input_deck(entry.path().string());
            application::validate_input_deck(deck, domain);
            ++examples;
        }
        require(examples >= 5, "Missing example input decks");
        bool missing_file = false;
        try { application::load_input_deck((std::filesystem::path(argv[1]) / "missing.json").string()); }
        catch (const std::runtime_error&) { missing_file = true; }
        require(missing_file, "Missing deck silently used defaults");
        std::printf("Input deck parsing, validation, and %u example files passed\n", examples);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
