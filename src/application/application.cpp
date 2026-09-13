#include "application/application.hpp"
#include "chrome/chrome.hpp"
#include "application/window.hpp"
#include "graphics/vulkan_backend.hpp"
#include "simulation/initial_conditions.hpp"

#include <chrono>
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
                const auto start = std::chrono::steady_clock::now();
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
                    const auto texture = graphics.prepare_scene(frame, viewport.width, viewport.height);
                    graphics.begin_imgui_frame();
                    window.begin_imgui_frame();
                    chrome.begin_frame();
                    viewport = chrome.draw_layout({texture});
                    const chrome::DrawData draw_data = chrome.finalize_frame();
                    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    if (texture) graphics.render_scene(frame, elapsed);
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
