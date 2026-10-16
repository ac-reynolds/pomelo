#pragma once
#include "graphics/camera.hpp"
#include "graphics/field_view.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <array>
#include <string_view>

namespace simulation { struct InitialConditions; struct SmokeParameters; }

namespace graphics { // Shared Vulkan, simulation, and visualization facilities.

struct SimulationOutput { FieldView density, pressure; };

struct Frame {
    void* command_buffer{};
    std::uint32_t image_index{};
    std::uint32_t frame_index{};
    bool valid{};
};

// Owns one coherent Vulkan object graph. Its lifetime guarantees that child GPU
// resources are destroyed before their device/instance.
class VulkanBackend {
public:
    VulkanBackend(void* native_window, const simulation::InitialConditions& initial,
                  const simulation::SmokeParameters&, std::span<const std::array<float, 3>> sources,
                  uint32_t pressure_iterations = 64);
    ~VulkanBackend();
    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend& operator=(VulkanBackend&&) = delete;
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    [[nodiscard]] std::string_view simulation_status() const;
    [[nodiscard]] Frame begin_frame();
    [[nodiscard]] SimulationOutput step_simulation(Frame frame);
    void render_volume(Frame frame, const FieldView& field, const OrbitCamera& camera, float domain_box_opacity);
    void end_frame(Frame frame);
    void wait_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphics
