#pragma once
#include "graphics/camera.hpp"
#include "graphics/field_view.hpp"
#include "graphics/render_target.hpp"

namespace graphics {
struct VolumePipeline {
    VkDescriptorSetLayout field_layout{};
    VkRenderPass render_pass{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkFormat color_format{};
    VkFormat depth_format{};
};

// One immutable image binding. The shared pipeline and field image must outlive
// this view and all commands recorded from it. Keep the pipeline at a stable address.
struct VolumeView {
    const VolumePipeline* pipeline{};
    FieldView field;
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
};

void create_volume_pipeline(VolumePipeline&, VkPhysicalDevice, VkDevice);
void destroy_volume_pipeline(VolumePipeline&, VkDevice);
void create_volume_view(VolumeView&, VkDevice, const VolumePipeline&, const FieldView&);
void destroy_volume_view(VolumeView&, VkDevice);
// Caller waits for GPU completion before destroying resources.
void record_volume_view(const VolumeView&, const RenderTarget&, VkCommandBuffer, const OrbitCamera&);
} // namespace graphics
