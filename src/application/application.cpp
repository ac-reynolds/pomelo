#include "application/application.hpp"
#include "chrome/chrome.hpp"
#include "platform/platform.hpp"
#include "graphics/scene_renderer.hpp"
#include "graphics/vulkan_backend.hpp"

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
                bool running = true;
                while (running) {
                    running = platform.poll_events();
                    if (!running) break;
                    if (platform.minimized()) {
                        platform.delay(10);
                        continue;
                    }
                    const graphics::ViewportImage image = graphics::render_scene(graphics, viewport.width, viewport.height);
                    graphics.begin_imgui_frame();
                    platform.begin_imgui_frame();
                    chrome.begin_frame();
                    viewport = chrome.draw_layout({image.texture_handle});
                    const chrome::DrawData draw_data = chrome.finalize_frame();
                    const graphics::Frame frame = graphics.begin_frame();
                    if (frame.valid) {
                        graphics.render_imgui(draw_data.native, frame.command_buffer);
                        graphics.end_frame(frame);
                    }
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
