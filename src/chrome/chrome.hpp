#pragma once

#include <cstdint>
#include <memory>

namespace chrome {

struct DrawData { void* native{}; };
struct TextureHandle { std::uint64_t native{}; };
struct ViewportState {
    int width{};
    int height{};
    bool hovered{};
    bool focused{};
};

class Chrome {
public:
    Chrome();
    ~Chrome();
    Chrome(const Chrome&) = delete;
    Chrome& operator=(const Chrome&) = delete;

    void begin_frame();
    [[nodiscard]] ViewportState draw_layout(TextureHandle viewport_texture = {});
    [[nodiscard]] DrawData finalize_frame();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chrome
