#pragma once
#include "graphics/camera.hpp"

// Internal graphics resources. The Vulkan backend owns their lifetime and waits
// for GPU completion before calling the destruction functions.
#include "graphics/field_view.hpp"
#include "graphics/render_target.hpp"
#include <span>
#include <vector>
#include <array>


namespace graphics {

struct VolumeViewResources {
    VkDescriptorSetLayout grid_layout{};
    VkDescriptorPool grid_pool{};
    std::vector<VkDescriptorSet> grid_sets;
    std::vector<FieldView> fields;
    VkRenderPass render_pass{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkFormat color_format{};
    VkFormat depth_format{};
};

void create_volume_view(VolumeViewResources& view, VkPhysicalDevice physical_device, VkDevice device,
                  std::span<const FieldView> fields);
void destroy_volume_view(VolumeViewResources& view, VkDevice device);
void record_volume_view(const VolumeViewResources& view, const RenderTarget& target,
                  VkCommandBuffer command_buffer, const OrbitCamera& camera, const FieldView& field);

} // namespace graphics
