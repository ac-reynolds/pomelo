#pragma once
#include "graphics/image.hpp"
#include <array>
#include <cstdint>

namespace graphics {
struct FieldDomain {
    std::array<uint32_t, 3> dimensions{};
    std::array<float, 3> origin{};
    float spacing{};
};
// Borrowed image/version description. The owner must outlive all GPU consumers.
struct FieldView {
    gpu::Image image;
    VkFormat format{};
    VkImageLayout layout{};
    FieldDomain domain;
    uint64_t version{};
};
// Versions can change while a descriptor continues to reference the same image.
inline bool same_field_binding(const FieldView& a, const FieldView& b) {
    return a.image.image == b.image.image && a.image.view == b.image.view &&
        a.format == b.format && a.layout == b.layout &&
        a.domain.dimensions == b.domain.dimensions && a.domain.origin == b.domain.origin &&
        a.domain.spacing == b.domain.spacing;
}
}
