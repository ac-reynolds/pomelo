#pragma once
#include "graphics/image.hpp"
#include "simulation/initial_conditions.hpp"
#include <array>
#include <cstdint>

namespace simulation {
inline constexpr float timestep = 1.0f / 60;
struct Advection {
    std::array<gpu::Image, 2> density;
    gpu::Image velocity;
    std::array<uint32_t, 3> dimensions{};
    float spacing{};
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool pool{};
    std::array<VkDescriptorSet, 2> sets{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    uint64_t submitted_steps{};
};
void create_advection(Advection&, VkPhysicalDevice, VkDevice, VkQueue, uint32_t queue_family,
                      const InitialConditions&);
// Caller waits for GPU completion before destruction.
void destroy_advection(Advection&, VkDevice);
// Records one step and returns the new image index; does not advance submitted_steps.
uint32_t record_advection(const Advection&, VkCommandBuffer, float dt = timestep);
}
