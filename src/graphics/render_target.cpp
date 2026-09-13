#include "graphics/render_target.hpp"
#include <stdexcept>
#include <string>
namespace {
void check(VkResult r) { if (r != VK_SUCCESS) throw std::runtime_error("Render target Vulkan error: " + std::to_string(r)); }
}
void graphics::create_render_target(RenderTarget& target, const RenderTargetFormat& format,
                                  VkPhysicalDevice physical, VkDevice device, VkExtent2D extent) {
    try {
        target.extent = extent;
        gpu::create_image(target.color, physical, device, {extent.width, extent.height, 1}, VK_IMAGE_TYPE_2D, format.color,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        gpu::create_image(target.depth, physical, device, {extent.width, extent.height, 1}, VK_IMAGE_TYPE_2D, format.depth,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        const VkImageView views[] = {target.color.view, target.depth.view};
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = format.render_pass;
        framebuffer.attachmentCount = 2;
        framebuffer.pAttachments = views;
        framebuffer.width = extent.width;
        framebuffer.height = extent.height;
        framebuffer.layers = 1;
        check(vkCreateFramebuffer(device, &framebuffer, nullptr, &target.framebuffer));
    } catch (...) {
        destroy_render_target(target, device);
        throw;
    }
}

void graphics::destroy_render_target(RenderTarget& target, VkDevice device) {
    if (target.framebuffer) vkDestroyFramebuffer(device, target.framebuffer, nullptr);
    gpu::destroy_image(target.depth, device);
    gpu::destroy_image(target.color, device);
    target = {};
}

