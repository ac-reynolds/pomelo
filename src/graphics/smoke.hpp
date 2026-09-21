#pragma once
#include "graphics/advection.hpp"

namespace simulation {
struct SmokeParameters {
    std::array<float, 3> center{}; // World-space center; +Y is up.
    float radius{};
    // Rates are per simulated second; buoyancy is acceleration per heat unit.
    float density_rate = 3.0f;
    float heat_rate = 4.0f;
    float cooling = 0.6f;
    float dissipation = 0.2f;
    float buoyancy = 1.0f;
    bool enabled = true;
};
SmokeParameters smoke_parameters(const graphics::FieldDomain&);
struct Smoke {
    std::array<graphics::FieldView, 2> density, velocity, temperature;
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool pool{};
    std::array<VkDescriptorSet, 2> sets{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
};
void create_smoke(Smoke&, VkPhysicalDevice, VkDevice, const SimulationFields&);
void destroy_smoke(Smoke&, VkDevice);
// Updates only the advection destinations, before divergence and projection.
void record_smoke(const Smoke&, VkCommandBuffer, const AdvectionStep&, const SmokeParameters&);
}
