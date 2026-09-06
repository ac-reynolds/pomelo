#pragma once

#include <cstdint>

namespace graphics { class VulkanBackend; }

namespace renderer {

struct ViewportImage { std::uint64_t texture_handle{}; };

class SceneRenderer {
public:
    explicit SceneRenderer(graphics::VulkanBackend& graphics);
    [[nodiscard]] ViewportImage render(int width, int height);

private:
    graphics::VulkanBackend* graphics_{};
};

} // namespace renderer
