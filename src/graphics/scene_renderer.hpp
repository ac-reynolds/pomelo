#pragma once

#include <cstdint>

namespace graphics { class VulkanBackend; }

namespace graphics {

struct ViewportImage { std::uint64_t texture_handle{}; };

[[nodiscard]] ViewportImage render_scene(VulkanBackend& graphics, int width, int height);

} // namespace graphics
