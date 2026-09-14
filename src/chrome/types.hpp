#pragma once
#include <cstdint>

namespace chrome {
struct DrawData { void* native{}; };
struct TextureHandle { std::uint64_t native{}; };
struct Bounds { float x{}, y{}, width{}, height{}; };
enum class DisplayField { density, divergence };
struct PanelState { bool expanded{true}; };
struct ViewportState {
    int width{};
    int height{};
    bool hovered{};
    bool focused{};
    float orbit_dx{};
    float orbit_dy{};
};
} // namespace chrome
