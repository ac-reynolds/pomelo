#include "graphics/scene_renderer.hpp"
#include "graphics/vulkan_backend.hpp"

graphics::SceneRenderer::SceneRenderer(VulkanBackend& graphics) : graphics_(&graphics) {}

graphics::ViewportImage graphics::SceneRenderer::render(int, int) {
    // The offscreen viewport image and scene pipeline will be created here.
    // An empty handle tells Chrome to leave the viewport panel blank for now.
    return {};
}
