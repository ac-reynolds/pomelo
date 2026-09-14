#pragma once

#include "chrome/types.hpp"
#include <string_view>

namespace chrome {

// Owns the sole Dear ImGui context. The class prevents copying and guarantees all
// UI frame operations occur while that context is alive.
class Chrome {
public:
    Chrome();
    ~Chrome();
    Chrome(const Chrome&) = delete;
    Chrome& operator=(const Chrome&) = delete;

    void begin_frame();
    [[nodiscard]] ViewportState draw_layout(TextureHandle viewport_texture = {},
                                            std::string_view status = "Ready");
    [[nodiscard]] DrawData finalize_frame();
    [[nodiscard]] DisplayField display_field() const { return display_field_; }

private:
    PanelState simulation_;
    PanelState rendering_;
    DisplayField display_field_{DisplayField::divergence};
};

} // namespace chrome
