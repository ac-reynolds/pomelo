#pragma once
#include "graphics/field_view.hpp"
#include <vector>

namespace simulation {
struct PressureSettings {
    uint32_t iterations{64}; // Interactive budget; residual error is expected.
    float omega{2.0f / 3.0f};
    float absolute_tolerance{1e-5f};
    float relative_tolerance{1e-3f};
};
struct PressureBuffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
};
struct PressureDiagnostics {
    float mean_divergence{}, rhs_rms{}, mean_pressure{}, residual_rms{};
    bool finite{};
};
bool pressure_converged(const PressureDiagnostics&, const PressureSettings& = {});
struct PressureSolve {
    graphics::FieldView divergence;
    std::array<graphics::FieldView, 2> pressure;
    VkDescriptorSetLayout descriptor_layout{};
    VkDescriptorPool pool{};
    std::array<VkDescriptorSet, 2> sets{};
    VkPipelineLayout layout{};
    VkPipeline jacobi{}, reduce{}, statistics_pipeline{}, normalize{};
    PressureBuffer partials, statistics;
    std::vector<PressureBuffer> readbacks;
    uint32_t groups{};
};
void create_pressure(PressureSolve&, VkPhysicalDevice, VkDevice,
    const graphics::FieldView& divergence, const std::array<graphics::FieldView, 2>& pressure,
    uint32_t frame_slots);
void destroy_pressure(PressureSolve&, VkDevice);
// Cold start; returns the actual final image, independent of simulation-step parity.
graphics::FieldView record_pressure(const PressureSolve&, VkCommandBuffer, float dt,
    uint64_t version, uint32_t frame_slot, const PressureSettings& = {});
// Caller must wait for the fence protecting this slot before reading/reusing it.
PressureDiagnostics read_pressure_diagnostics(const PressureSolve&, VkDevice, uint32_t frame_slot);
}
