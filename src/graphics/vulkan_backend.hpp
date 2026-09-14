#pragma once
#include "graphics/camera.hpp"
#include "graphics/field_view.hpp"

#include <cstdint>
#include <memory>

namespace simulation { struct InitialConditions; }

namespace graphics { // Shared Vulkan, simulation, and visualization facilities.

// Native handles are intentionally opaque here so Vulkan never leaks into
// application code. Chrome unwraps them only in its private implementation.
struct BackendInfo {
    void* instance{};
    void* physical_device{};
    void* device{};
    void* queue{};
    void* render_pass{};
    void* descriptor_pool{};
    std::uint32_t queue_family{};
    std::uint32_t image_count{};
};

struct SimulationOutput { FieldView density, divergence; };

struct Frame {
    void* command_buffer{};
    std::uint32_t image_index{};
    std::uint32_t frame_index{};
    bool valid{};
};

// Owns one coherent Vulkan object graph. Its lifetime guarantees that child GPU
// resources and the ImGui Vulkan adapter are destroyed before their device/instance.
class VulkanBackend {
public:
    VulkanBackend(void* native_window, const simulation::InitialConditions& initial);
    ~VulkanBackend();
    VulkanBackend(VulkanBackend&&) = delete;
    VulkanBackend& operator=(VulkanBackend&&) = delete;
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    [[nodiscard]] BackendInfo backend_info() const;
    void begin_imgui_frame();
    [[nodiscard]] Frame begin_frame();
    [[nodiscard]] std::uint64_t prepare_viewport(Frame frame, int width, int height);
    [[nodiscard]] SimulationOutput step_simulation(Frame frame);
    void render_volume(Frame frame, const FieldView& field, const OrbitCamera& camera);
    void render_imgui(void* draw_data, Frame frame);
    void end_frame(Frame frame);
    void wait_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphics
