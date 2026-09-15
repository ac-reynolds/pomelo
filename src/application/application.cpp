#include "application/application.hpp"
#include "chrome/chrome.hpp"
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
        {
            chrome::Chrome chrome;
            window.initialize_imgui_platform();
            try {
                const auto initial = simulation::initial_conditions();
                graphics::VulkanBackend graphics(window.native_window(), initial);
                chrome::ViewportState viewport{1, 1, false, false};
                graphics::OrbitCamera camera;
                bool running = true;
                while (running) {
                    running = window.poll_events();
                    if (!running) break;
                    if (window.minimized()) {
                        window.delay(10);
                        continue;
                    }
                    const graphics::Frame frame = graphics.begin_frame();
                    if (!frame.valid) continue;
                    const auto texture = graphics.prepare_viewport(frame, viewport.width, viewport.height);
                    graphics.begin_imgui_frame();
                    window.begin_imgui_frame();
                    chrome.begin_frame();
                    viewport = chrome.draw_layout({texture}, graphics.simulation_status());
                    const chrome::DrawData draw_data = chrome.finalize_frame();
                    constexpr float sensitivity = 0.005f; // Radians per logical screen pixel.
                    camera.yaw = std::remainder(camera.yaw + viewport.orbit_dx * sensitivity,
                                                2 * std::numbers::pi_v<float>);
                    camera.pitch = std::clamp(camera.pitch + viewport.orbit_dy * sensitivity, -1.5f, 1.5f);
                    if (texture) {
                        const auto output = graphics.step_simulation(frame);
                        const auto& field = chrome.display_field() == chrome::DisplayField::density
                            ? output.density : output.pressure;
                        graphics.render_volume(frame, field, camera);
                    }
                    graphics.render_imgui(draw_data.native, frame);
                    graphics.end_frame(frame);
                }
                graphics.wait_idle();
            } catch (...) {
                window.shutdown_imgui_platform();
                throw;
            }
            window.shutdown_imgui_platform();
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pomelo: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
