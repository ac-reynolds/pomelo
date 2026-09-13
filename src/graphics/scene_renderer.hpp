#pragma once

// Internal graphics resources. The Vulkan backend owns their lifetime and waits
// for GPU completion before calling the destruction functions.
#include "graphics/image.hpp"
#include <array>

namespace geometry { struct UniformGrid; }

namespace graphics {

struct SceneTarget {
    gpu::Image color;
    gpu::Image depth;
    VkFramebuffer framebuffer{};
    VkExtent2D extent{};
};

struct SceneResources {
    VkDescriptorSetLayout grid_layout{};
    VkDescriptorPool grid_pool{};
    std::array<VkDescriptorSet, 2> grid_sets{};
    float origin_spacing[4]{};
    float center[3]{};
    float radius{};
    VkRenderPass render_pass{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkFormat color_format{};
    VkFormat depth_format{};
};

void create_scene(SceneResources& scene, VkPhysicalDevice physical_device, VkDevice device,
                  const geometry::UniformGrid& grid, const std::array<VkImageView, 2>& density);
void destroy_scene(SceneResources& scene, VkDevice device);
void create_scene_target(SceneTarget& target, const SceneResources& scene,
                         VkPhysicalDevice physical_device, VkDevice device, VkExtent2D extent);
void destroy_scene_target(SceneTarget& target, VkDevice device);
void record_scene(const SceneResources& scene, const SceneTarget& target,
                  VkCommandBuffer command_buffer, double elapsed_seconds, uint32_t density_index);

} // namespace graphics
