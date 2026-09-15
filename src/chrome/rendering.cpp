#include "chrome/rendering.hpp"
#include "chrome/panel.hpp"
#include <imgui.h>

void chrome::draw_rendering(Bounds bounds, PanelState& state, DisplayField& selection) {
    if (begin_panel("rendering", bounds, state, true)) {
        ImGui::TextUnformatted("Field");
        if (ImGui::RadioButton("Density", selection == DisplayField::density))
            selection = DisplayField::density;
        if (ImGui::RadioButton("Pressure", selection == DisplayField::pressure))
            selection = DisplayField::pressure;
    }
    end_panel();
}
