#pragma once

namespace graphics {
// Orbit around the domain center at its fitted viewing distance. Angles are radians.
struct OrbitCamera {
    float yaw{};
    float pitch{-0.35f};
    float zoom{1.0f}; // Larger values move closer; 1 preserves the fitted view.
};
}
