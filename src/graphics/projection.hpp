#pragma once
#include "graphics/field_view.hpp"

namespace simulation {
struct Projection {
    std::array<graphics::FieldView, 2> velocity;
    std::array<graphics::FieldView, 2> pressure;
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool pool{};
    // Indexed by velocity index * 2 + pressure index; immutable bindings.
    std::array<VkDescriptorSet, 4> sets{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
};
void create_projection(Projection&, VkPhysicalDevice, VkDevice,
    const std::array<graphics::FieldView, 2>& velocity,
    const std::array<graphics::FieldView, 2>& pressure);
void destroy_projection(Projection&, VkDevice);
// Subtract dt * forward_gradient(pressure) from this timestep's velocity in place.
// Pressure/velocity versions must match. Zero dt leaves velocity unchanged.
void record_projection(const Projection&, VkCommandBuffer, const graphics::FieldView& velocity,
    const graphics::FieldView& pressure, float dt);
}
