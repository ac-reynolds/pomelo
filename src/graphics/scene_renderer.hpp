#pragma once

// Internal graphics resources. The Vulkan backend owns their lifetime and waits
// for GPU completion before calling the destruction functions.
#include <vulkan/vulkan.h>

namespace graphics {

struct SceneImage {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
};

struct SceneTarget {
    SceneImage color;
    SceneImage depth;
    VkFramebuffer framebuffer{};
    VkExtent2D extent{};
};

struct SceneResources {
    VkBuffer mesh{};
    VkDeviceMemory mesh_memory{};
    VkRenderPass render_pass{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkFormat color_format{};
    VkFormat depth_format{};
};

void create_scene(SceneResources& scene, VkPhysicalDevice physical_device, VkDevice device);
void destroy_scene(SceneResources& scene, VkDevice device);
void create_scene_target(SceneTarget& target, const SceneResources& scene,
                         VkPhysicalDevice physical_device, VkDevice device, VkExtent2D extent);
void destroy_scene_target(SceneTarget& target, VkDevice device);
void record_scene(const SceneResources& scene, const SceneTarget& target,
                  VkCommandBuffer command_buffer, double elapsed_seconds);

} // namespace graphics
