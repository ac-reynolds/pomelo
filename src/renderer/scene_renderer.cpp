#include "renderer/scene_renderer.hpp"
#include "renderer/vulkan_backend.hpp"

renderer::SceneRenderer::SceneRenderer(graphics::VulkanBackend& graphics) : graphics_(&graphics) {}

renderer::ViewportImage renderer::SceneRenderer::render(int, int) {
    // The offscreen viewport image and scene pipeline will be created here.
    // An empty handle tells Chrome to leave the viewport panel blank for now.
    return {};
}
