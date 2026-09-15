#pragma once
#include "graphics/field_view.hpp"

namespace simulation {
struct Divergence {
    std::array<graphics::FieldView, 2> velocity;
    graphics::FieldView output;
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool pool{};
    std::array<VkDescriptorSet, 2> sets{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
};
void create_divergence(Divergence&, VkPhysicalDevice, VkDevice,
    const std::array<graphics::FieldView, 2>& velocity, const graphics::FieldView& output);
void destroy_divergence(Divergence&, VkDevice);
// Backward differences with periodic boundaries, paired with projection's forward gradient.
// Call after advection for the pressure RHS, and after projection to refresh the field.
void record_divergence(const Divergence&, VkCommandBuffer, const graphics::FieldView& velocity);
}
