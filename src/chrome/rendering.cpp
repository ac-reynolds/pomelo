#include "chrome/rendering.hpp"
#include "chrome/panel.hpp"

void chrome::draw_rendering(Bounds bounds, PanelState& state) {
    if (begin_panel("rendering", bounds, state, true)) {
        // Rendering controls belong here.
    }
    end_panel();
}
