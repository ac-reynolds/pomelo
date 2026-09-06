#pragma once

#include <cstdint>
#include <memory>

namespace graphics { // Shared scene and Vulkan rendering facilities.

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

struct Frame {
    void* command_buffer{};
    std::uint32_t image_index{};
    bool valid{};
};

class VulkanBackend {
public:
    explicit VulkanBackend(void* native_window);
    ~VulkanBackend();
    VulkanBackend(VulkanBackend&&) noexcept;
    VulkanBackend& operator=(VulkanBackend&&) noexcept;
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    [[nodiscard]] BackendInfo backend_info() const;
    void begin_imgui_frame();
    [[nodiscard]] Frame begin_frame();
    void render_imgui(void* draw_data, void* command_buffer);
    void end_frame(Frame frame);
    void wait_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphics
