#include "application/input_deck.hpp"
#include "graphics/initial_conditions.hpp"
#include "graphics/smoke.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <numbers>
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

void check_demo_initial_conditions(const graphics::FieldDomain& domain) {
    const auto n = domain.dimensions;
    for (const auto* preset : {"taylor_green", "double_shear_layer", "vortex_ring"}) {
        application::Settings settings;
        settings.preset = preset;
        settings.thermal_buoyancy = false;
        settings.density_dissipation = 0;
        const auto initial = application::make_initial_conditions(settings, domain);
        simulation::validate_initial_conditions(initial);
        const auto repeated = application::make_initial_conditions(settings, domain);
        require(initial.density.values == repeated.density.values && initial.velocity == repeated.velocity,
            "Demo initialization is not deterministic");
        const auto parameters = application::make_smoke_parameters(settings, domain);
        require(parameters.heat_rate == 0 && parameters.buoyancy == 0 && parameters.dissipation == 0,
            "Passive dye settings did not disable thermal forces and explicit tracer loss");
        double max_divergence = 0, max_speed = 0, max_density = 0;
        double analytic_error = 0, analytic_energy = 0;
        auto index = [&](uint32_t x, uint32_t y, uint32_t z) {
            return geometry::cell_index(initial.density, x, y, z);
        };
        for (uint32_t z = 0; z < n[2]; ++z)
            for (uint32_t y = 0; y < n[1]; ++y)
                for (uint32_t x = 0; x < n[0]; ++x) {
                    const auto i = index(x, y, z);
                    const auto& v = initial.velocity[i];
                    const double density = initial.density.values[i];
                    require(density >= 0 && density <= 1.000001, "Demo tracer is outside [0,1]");
                    require(initial.temperature[i] == 0 && v[3] == 0, "Demo initialized heat or reserved velocity component");
                    max_density = std::max(max_density, density);
                    max_speed = std::max(max_speed, std::sqrt(double(v[0])*v[0] + double(v[1])*v[1] + double(v[2])*v[2]));
                    const double divergence = (double(v[0]) - initial.velocity[index((x+n[0]-1)%n[0],y,z)][0]
                        + double(v[1]) - initial.velocity[index(x,(y+n[1]-1)%n[1],z)][1]
                        + double(v[2]) - initial.velocity[index(x,y,(z+n[2]-1)%n[2])][2]) / domain.spacing;
                    max_divergence = std::max(max_divergence, std::abs(divergence));
                    if (settings.preset == "taylor_green" && n == std::array<uint32_t,3>{64,64,64}) {
                        constexpr double tau = 2 * std::numbers::pi;
                        const double a = tau*(x+.5)/n[0], b = tau*(y+.5)/n[1], c = tau*(z+.5)/n[2];
                        const double u = .5*std::sin(a)*std::cos(b)*std::cos(c);
                        const double w = -.5*std::cos(a)*std::sin(b)*std::cos(c);
                        analytic_error += (v[0]-u)*(v[0]-u) + (v[1]-w)*(v[1]-w);
                        analytic_energy += u*u + w*w;
                    }
                }
        require(max_divergence < 1e-4, "Demo is not divergence-free under the solver's backward stencil");
        require(max_speed > .05 && max_density > .5, "Demo initial fields are trivial or invisible");
        if (analytic_energy > 0)
            require(std::sqrt(analytic_error / analytic_energy) < .06, "Discrete TGV deviates too far from analytic TGV");
        std::printf("%s %ux%ux%u: max initial divergence %.3g, speed %.3g\n", preset,n[0],n[1],n[2],max_divergence,max_speed);
    }
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
        require(defaults.preset == "smoke" && defaults.thermal_buoyancy && defaults.density_dissipation == .2f &&
            defaults.camera_yaw == 0 && defaults.camera_pitch == -.35f && defaults.camera_zoom == 1 &&
            defaults.pressure_iterations == 64, "New settings changed legacy defaults");
        const auto legacy_parameters = application::make_smoke_parameters(defaults, domain);
        const auto old_parameters = simulation::smoke_parameters(domain);
        require(legacy_parameters.heat_rate == old_parameters.heat_rate && legacy_parameters.buoyancy == old_parameters.buoyancy &&
            legacy_parameters.dissipation == old_parameters.dissipation, "Legacy smoke parameters changed");
        auto nonthermal = defaults;
        nonthermal.thermal_buoyancy = false;
        require(application::make_smoke_parameters(nonthermal, domain).dissipation == .2f,
            "Disabling thermal buoyancy silently changed tracer dissipation");
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
        const auto demo = application::parse_input_deck(R"({"sources":[],"domain_box_opacity":0.2,
            "preset":"taylor_green","thermal_buoyancy":false,"density_dissipation":0,"camera_yaw":0.5,"camera_pitch":-0.4,
            "camera_zoom":2,"pressure_iterations":512})");
        require(demo.preset == "taylor_green" && !demo.thermal_buoyancy && demo.density_dissipation == 0 &&
            demo.camera_yaw == .5f && demo.camera_pitch == -.4f && demo.camera_zoom == 2 && demo.pressure_iterations == 512,
            "Demo fields were lost");
        application::validate_input_deck(demo, domain);
        for (const auto* member : {"\"preset\":\"unknown\"", "\"preset\":1", "\"thermal_buoyancy\":0",
             "\"thermal_buoyancy\":\"false\"", "\"density_dissipation\":-0.1", "\"density_dissipation\":10.1",
             "\"camera_yaw\":1e39", "\"camera_pitch\":1.51", "\"camera_pitch\":-1.51", "\"camera_zoom\":0.49", "\"camera_zoom\":3.01",
             "\"pressure_iterations\":0", "\"pressure_iterations\":4097", "\"pressure_iterations\":-1", "\"pressure_iterations\":1.5",
             "\"pressure_iterations\":64.0", "\"pressure_iterations\":1e2", "\"pressure_iterations\":true", "\"pressure_iterations\":\"64\""}) {
            const std::string field(member);
            reject("{\"sources\":[],\"domain_box_opacity\":0," + field + "}", field.substr(1,field.find('"',1)-1));
        }
        for (const auto* member : {"\"preset\":\"smoke\"", "\"thermal_buoyancy\":true", "\"density_dissipation\":0",
             "\"camera_yaw\":0", "\"camera_pitch\":0", "\"camera_zoom\":1", "\"pressure_iterations\":64"}) {
            const std::string field(member);
            reject("{\"sources\":[],\"domain_box_opacity\":0," + field + "," + field + "}", "duplicate key");
        }

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
        invalid = defaults; invalid.preset = "not_a_preset"; reject_domain(invalid,domain,"preset");
        invalid = defaults; invalid.camera_pitch = 1.6f; reject_domain(invalid,domain,"camera_pitch");
        invalid = defaults; invalid.camera_yaw = std::numeric_limits<float>::infinity(); reject_domain(invalid,domain,"camera_yaw");
        invalid = defaults; invalid.camera_zoom = 0.4f; reject_domain(invalid,domain,"camera_zoom");
        invalid = defaults; invalid.pressure_iterations = 0; reject_domain(invalid,domain,"pressure_iterations");
        invalid = defaults; invalid.pressure_iterations = 4097; reject_domain(invalid,domain,"pressure_iterations");
        invalid = defaults; invalid.pressure_iterations = 1; application::validate_input_deck(invalid, domain);
        invalid.pressure_iterations = 4096; application::validate_input_deck(invalid, domain);
        invalid = defaults; invalid.density_dissipation = -1; reject_domain(invalid,domain,"density_dissipation");
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
        check_demo_initial_conditions(domain);
        check_demo_initial_conditions({{19,17,15},{-2,1,3},.1f});
        std::printf("Input deck parsing, validation, and %u example files passed\n", examples);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
