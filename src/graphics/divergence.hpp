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
// Central differences with periodic boundaries; call after advection on the same queue.
void record_divergence(const Divergence&, VkCommandBuffer, const graphics::FieldView& velocity);
}
