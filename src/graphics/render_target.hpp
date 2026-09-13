#pragma once
#include "graphics/image.hpp"
namespace graphics {
struct RenderTarget {
    gpu::Image color, depth;
    VkFramebuffer framebuffer{};
    VkExtent2D extent{};
};
struct RenderTargetFormat { VkRenderPass render_pass{}; VkFormat color{}, depth{}; };
void create_render_target(RenderTarget&, const RenderTargetFormat&, VkPhysicalDevice, VkDevice, VkExtent2D);
void destroy_render_target(RenderTarget&, VkDevice);
}
