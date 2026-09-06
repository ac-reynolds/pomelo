#include "graphics/scene_renderer.hpp"
#include "graphics/vulkan_backend.hpp"

graphics::ViewportImage graphics::render_scene(VulkanBackend&, int, int) {
    // The offscreen viewport image and scene pipeline will be created here.
    // An empty handle tells Chrome to leave the viewport panel blank for now.
    return {};
}
