#include "application/application.hpp"
#include "chrome/chrome.hpp"
#include "platform/platform.hpp"
#include "graphics/vulkan_backend.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

int application::run() {
    try {
        platform::Platform platform;
        {
            chrome::Chrome chrome;
            platform.initialize_imgui_platform();
            try {
                graphics::VulkanBackend graphics(platform.native_window());
                chrome::ViewportState viewport{1, 1, false, false};
                const auto start = std::chrono::steady_clock::now();
                bool running = true;
                while (running) {
                    running = platform.poll_events();
                    if (!running) break;
                    if (platform.minimized()) {
                        platform.delay(10);
                        continue;
                    }
                    const graphics::Frame frame = graphics.begin_frame();
                    if (!frame.valid) continue;
                    const auto texture = graphics.prepare_scene(frame, viewport.width, viewport.height);
                    graphics.begin_imgui_frame();
                    platform.begin_imgui_frame();
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
                platform.shutdown_imgui_platform();
                throw;
            }
            platform.shutdown_imgui_platform();
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pomelo: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
