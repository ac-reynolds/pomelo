#include "application/application.hpp"
#include "application/window.hpp"
#include "application/input_deck.hpp"
#include "graphics/smoke.hpp"
#include "graphics/vulkan_backend.hpp"
#include "graphics/initial_conditions.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

namespace {
std::atomic_flag active_run = ATOMIC_FLAG_INIT;
}

void application::run(const Settings& settings, void (*poll_hook)()) {
    const auto deck = settings;
    const auto domain = simulation::smoke_domain();
    validate_settings(deck, domain);
    if (active_run.test_and_set()) throw std::runtime_error("A simulation is already running");
    try {
        const auto initial = simulation::smoke_initial_conditions();
        const auto smoke_parameters = simulation::smoke_parameters(domain);
        Window window;
        graphics::VulkanBackend graphics(window.native_window(), initial, smoke_parameters, deck.sources);
        graphics::OrbitCamera camera;
        float orbit_dx{}, orbit_dy{};
        while (true) {
            if (poll_hook) poll_hook();
            if (!window.poll_events(orbit_dx, orbit_dy)) break;
            constexpr float sensitivity = 0.005f; // Radians per logical screen pixel.
            camera.yaw = std::remainder(camera.yaw + orbit_dx * sensitivity,
                                        2 * std::numbers::pi_v<float>);
            camera.pitch = std::clamp(camera.pitch + orbit_dy * sensitivity, -1.5f, 1.5f);
            if (window.minimized()) {
                window.delay(10);
                continue;
            }
            const auto frame = graphics.begin_frame();
            if (!frame.valid) continue;
            const auto output = graphics.step_simulation(frame);
            graphics.render_volume(frame, output.density, camera, deck.domain_box_opacity);
            graphics.end_frame(frame);
        }
        graphics.wait_idle();
    } catch (...) {
        active_run.clear();
        throw;
    }
    active_run.clear();
}

int application::run(std::string_view input_filename) {
    try {
        run(input_filename.empty() ? default_settings() : load_input_deck(input_filename));
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pomelo: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
