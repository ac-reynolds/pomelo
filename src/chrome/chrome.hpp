#pragma once

#include <cstdint>

namespace chrome {

struct DrawData { void* native{}; };
struct TextureHandle { std::uint64_t native{}; };
struct ViewportState {
    int width{};
    int height{};
    bool hovered{};
    bool focused{};
};

// Owns the sole Dear ImGui context. The class prevents copying and guarantees all
// UI frame operations occur while that context is alive.
class Chrome {
public:
    Chrome();
    ~Chrome();
    Chrome(const Chrome&) = delete;
    Chrome& operator=(const Chrome&) = delete;

    void begin_frame();
    [[nodiscard]] ViewportState draw_layout(TextureHandle viewport_texture = {});
    [[nodiscard]] DrawData finalize_frame();

};

} // namespace chrome
