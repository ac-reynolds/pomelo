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
    bool signed_values{};
    float signed_scale{1}; // Magnitude mapped to full signed color/opacity.
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
};

void create_volume_pipeline(VolumePipeline&, VkPhysicalDevice, VkDevice);
void destroy_volume_pipeline(VolumePipeline&, VkDevice);
void create_volume_view(VolumeView&, VkDevice, const VolumePipeline&, const FieldView&, bool signed_values = false, float signed_scale = 1);
void destroy_volume_view(VolumeView&, VkDevice);
// Caller waits for GPU completion before destroying resources. The optional grid
// is composited on all six domain faces in the same volume draw.
void record_volume_view(const VolumeView&, const RenderTarget&, VkCommandBuffer, const OrbitCamera&, bool show_domain_grid = false);
} // namespace graphics
