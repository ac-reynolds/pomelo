#pragma once
#include "graphics/fields.hpp"

namespace simulation {
inline constexpr float timestep = 1.0f / 60;
struct Advection {
    std::array<graphics::FieldView, 2> inputs;
    std::array<graphics::FieldView, 2> velocity;
    std::array<graphics::FieldView, 2> temperature;
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool pool{};
    std::array<VkDescriptorSet, 2> sets{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
};
struct AdvectionStep {
    graphics::FieldView input, velocity, output, velocity_output;
    graphics::FieldView temperature, temperature_output;
    float dt{};
};
void create_advection(Advection&, VkPhysicalDevice, VkDevice,
    const std::array<graphics::FieldView, 2>& density, const std::array<graphics::FieldView, 2>& velocity,
    const std::array<graphics::FieldView, 2>& temperature);
// Destroys operation resources only; fields outlive their borrowers.
void destroy_advection(Advection&, VkDevice);
AdvectionStep prepare_advection(const SimulationFields&, float dt = timestep);
void record_advection(const Advection&, VkCommandBuffer, const AdvectionStep&);
// Call only after successful queue submission.
void commit_step(SimulationFields&, const AdvectionStep&);
}
