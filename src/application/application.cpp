#include "application/application.hpp"
#include "application/window.hpp"
#include "graphics/vulkan_backend.hpp"
#include "graphics/initial_conditions.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

int application::run() {
    try {
        Window window;
        const auto initial = simulation::smoke_initial_conditions();
        graphics::VulkanBackend graphics(window.native_window(), initial);
        graphics::OrbitCamera camera;
        float orbit_dx{}, orbit_dy{};
        while (window.poll_events(orbit_dx, orbit_dy)) {
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
            graphics.render_volume(frame, output.density, camera);
            graphics.end_frame(frame);
        }
        graphics.wait_idle();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pomelo: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
