#include "vulkan_test_helpers.hpp"
#include "application/settings.hpp"
#include "graphics/pressure.hpp"
#include "graphics/projection.hpp"
#include "graphics/smoke.hpp"

namespace {
application::Settings dye_settings(const char* preset) {
    application::Settings settings;
    settings.preset = preset;
    settings.sources.clear();
    settings.thermal_buoyancy = false;
    settings.density_dissipation = 0;
    settings.pressure_iterations = 129; // Nondefault and odd: exercise the configured pressure output.
    return settings;
}

// Exercise the production operators and settings helpers without a window.
// These are boundedness/integration regressions, not CFD benchmark validation.
struct Simulation {
    Context& context;
    simulation::SimulationFields fields;
    simulation::Advection advection;
    simulation::Smoke smoke;
    simulation::Divergence divergence;
    simulation::PressureSolve pressure;
    simulation::Projection projection;

    explicit Simulation(Context& c) : context(c) {}
    ~Simulation() {
        vkDeviceWaitIdle(context.device);
        simulation::destroy_projection(projection, context.device);
        simulation::destroy_pressure(pressure, context.device);
        simulation::destroy_divergence(divergence, context.device);
        simulation::destroy_smoke(smoke, context.device);
        simulation::destroy_advection(advection, context.device);
        simulation::destroy_fields(fields, context.device);
    }
    void initialize(const simulation::InitialConditions& initial) {
        auto& c = context;
        simulation::create_fields(fields, c.physical, c.device, c.queue, c.family, initial);
        const std::array density{simulation::density_view(fields, 0, 0), simulation::density_view(fields, 1, 0)};
        const std::array velocity{simulation::velocity_view(fields, 0, 0), simulation::velocity_view(fields, 1, 0)};
        const std::array temperature{simulation::temperature_view(fields, 0, 0), simulation::temperature_view(fields, 1, 0)};
        const std::array pressures{simulation::pressure_view(fields, 0, 0), simulation::pressure_view(fields, 1, 0)};
        simulation::create_advection(advection, c.physical, c.device, density, velocity, temperature);
        simulation::create_smoke(smoke, c.physical, c.device, fields);
        simulation::create_divergence(divergence, c.physical, c.device, velocity, simulation::divergence_view(fields, 0));
        simulation::create_pressure(pressure, c.physical, c.device, simulation::divergence_view(fields, 0), pressures, 2);
        simulation::create_projection(projection, c.physical, c.device, velocity, pressures);
    }
};

void evolve_preset(Context& c, const char* preset) {
    constexpr graphics::FieldDomain domain{{24, 24, 24}, {-1, -1, -1}, 2.0f / 24};
    constexpr unsigned steps = 12;
    const auto settings = dye_settings(preset);
    const auto initial = application::make_initial_conditions(settings, domain);
    const auto parameters = application::make_smoke_parameters(settings, domain);
    const simulation::PressureSettings pressure_settings{.iterations = settings.pressure_iterations};
    require(parameters.buoyancy == 0 && parameters.heat_rate == 0 && parameters.dissipation == 0,
        "Passive-dye settings left a thermal or density-decay term enabled");
    Simulation sim(c);
    sim.initialize(initial);
    for (unsigned i = 0; i < steps; ++i) {
        auto& slot = c.slots[i % 2];
        begin(c, slot);
        const auto step = simulation::prepare_advection(sim.fields);
        simulation::record_advection(sim.advection, slot.command, step);
        simulation::record_smoke(sim.smoke, slot.command, step, parameters, settings.sources);
        simulation::record_divergence(sim.divergence, slot.command, step.velocity_output);
        const auto pressure = simulation::record_pressure(sim.pressure, slot.command,
            step.dt, step.output.version, i % 2, pressure_settings);
        require(pressure.image.image == sim.fields.pressure[settings.pressure_iterations % 2].image &&
            pressure.version == step.output.version, "Configured pressure budget did not select the expected image/version");
        simulation::record_projection(sim.projection, slot.command, step.velocity_output, pressure, step.dt);
        simulation::record_divergence(sim.divergence, slot.command, step.velocity_output);
        submit(c, slot);
        simulation::commit_step(sim.fields, step);
    }
    const VkExtent3D extent{domain.dimensions[0], domain.dimensions[1], domain.dimensions[2]};
    const auto density = read_image(c, simulation::current_density(sim.fields).image, extent, 1, VK_IMAGE_LAYOUT_GENERAL);
    const auto velocity = read_image(c, simulation::current_velocity(sim.fields).image, extent, 4, VK_IMAGE_LAYOUT_GENERAL);
    const auto temperature = read_image(c, simulation::current_temperature(sim.fields).image, extent, 1, VK_IMAGE_LAYOUT_GENERAL);
    double squared_change = 0, maximum_change = 0, maximum_speed = 0;
    for (size_t i = 0; i < density.size(); ++i) {
        require(std::isfinite(density[i]) && density[i] >= -1e-6f && density[i] <= 1.0f + 1e-6f,
            "Evolved dye is nonfinite or outside [0,1]");
        require(temperature[i] == 0, "Passive-dye evolution generated heat");
        const double change = double(density[i]) - initial.density.values[i];
        squared_change += change * change;
        maximum_change = std::max(maximum_change, std::abs(change));
        double speed_squared = 0;
        for (unsigned axis = 0; axis < 4; ++axis) {
            require(std::isfinite(velocity[4 * i + axis]), "Evolved velocity is nonfinite");
            if (axis < 3) speed_squared += double(velocity[4 * i + axis]) * velocity[4 * i + axis];
        }
        require(velocity[4 * i + 3] == 0, "Reserved velocity component is nonzero");
        maximum_speed = std::max(maximum_speed, std::sqrt(speed_squared));
    }
    const double rms_change = std::sqrt(squared_change / density.size());
    require(rms_change > 1e-4 && maximum_change > 1e-3,
        "Preset dye did not evolve over twelve simulation steps");
    require(maximum_speed > 1e-4, "Preset flow vanished");
    const auto diagnostic = simulation::read_pressure_diagnostics(sim.pressure, c.device, (steps - 1) % 2);
    require(diagnostic.finite, "Preset pressure diagnostics are nonfinite");
    std::printf("%s: %u steps at 24^3, %u pressure iterations, dye RMS change %.6g, max change %.6g, max speed %.6g\n",
        preset, steps, settings.pressure_iterations, rms_change, maximum_change, maximum_speed);
}

void disabled_thermal_with_existing_heat(Context& c) {
    constexpr graphics::FieldDomain domain{{12, 12, 12}, {-1, -1, -1}, 2.0f / 12};
    constexpr float dt = 0.2f;
    const auto settings = dye_settings("taylor_green");
    const auto parameters = application::make_smoke_parameters(settings, domain);
    auto initial = simulation::smoke_initial_conditions(domain.dimensions, domain.origin, domain.spacing);
    std::fill(initial.density.values.begin(), initial.density.values.end(), 0.4f);
    std::fill(initial.temperature.begin(), initial.temperature.end(), 2.0f);
    for (auto& velocity : initial.velocity) velocity = {0.3f, -0.2f, 0.1f, 0};
    Simulation sim(c);
    sim.initialize(initial);
    const VkExtent3D extent{domain.dimensions[0], domain.dimensions[1], domain.dimensions[2]};
    // Each case uses a freshly initialized ping-pong destination. The nonempty
    // source case proves thermal=false also suppresses new heat injection.
    for (unsigned source_case = 0; source_case < 2; ++source_case) {
        const auto step = simulation::prepare_advection(sim.fields, dt);
        const std::vector<std::array<float, 3>> sources = source_case
            ? std::vector<std::array<float, 3>>{parameters.center} : settings.sources;
        auto& slot = c.slots[source_case];
        begin(c, slot);
        simulation::record_smoke(sim.smoke, slot.command, step, parameters, sources);
        submit(c, slot);
        simulation::commit_step(sim.fields, step);
        const auto density = read_image(c, step.output.image, extent, 1, VK_IMAGE_LAYOUT_GENERAL);
        const auto velocity = read_image(c, step.velocity_output.image, extent, 4, VK_IMAGE_LAYOUT_GENERAL);
        const auto temperature = read_image(c, step.temperature_output.image, extent, 1, VK_IMAGE_LAYOUT_GENERAL);
        bool dye_injected = false;
        const double expected_heat = 2.0 * std::exp(-double(parameters.cooling) * dt);
        for (size_t i = 0; i < density.size(); ++i) {
            if (!source_case) require(density[i] == 0.4f, "Source-free passive dye was injected or dissipated");
            require(std::isfinite(density[i]) && density[i] >= 0.4f && density[i] <= 1,
                "Disabled-thermal source produced invalid dye");
            dye_injected |= density[i] > 0.4f;
            require(std::abs(temperature[i] - expected_heat) < 2e-6,
                "Thermal-disabled source injected heat or changed cooling");
            for (unsigned axis = 0; axis < 4; ++axis)
                require(velocity[4 * i + axis] == initial.velocity[i][axis],
                    "Thermal-disabled source accelerated already-warm fluid");
        }
        require(dye_injected == bool(source_case), "Source test did not exercise the intended injection state");
    }
    std::printf("Thermal disabled: existing heat caused no buoyancy; sources injected dye without heat\n");
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Context c;
    try {
        if (!create(c)) { destroy(c); return 77; }
        for (const char* preset : {"taylor_green", "double_shear_layer", "vortex_ring"}) evolve_preset(c, preset);
        disabled_thermal_with_existing_heat(c);
        destroy(c);
        require(c.errors == 0, "Vulkan validation errors");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        destroy(c);
        return 1;
    }
}
