#include "chrome/panel.hpp"
#include <imgui.h>
#include <algorithm>

bool chrome::begin_panel(const char* name, Bounds bounds, PanelState& state, bool right) {
    ImGui::SetNextWindowPos({bounds.x, bounds.y});
    ImGui::SetNextWindowSize({bounds.width, bounds.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {1, 1});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2, 2});
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    const bool visible = ImGui::Begin(name, nullptr, flags);
    ImGui::PopStyleVar(2);
    if (!visible) return false;
    const char* arrow = state.expanded ? (right ? ">" : "<") : (right ? "<" : ">");
    if (ImGui::Button(arrow)) state.expanded = !state.expanded;
    ImGui::SetItemTooltip("%s %s", state.expanded ? "Collapse" : "Expand", name);
    if (state.expanded) {
        ImGui::SameLine();
        ImGui::TextUnformatted(name);
        ImGui::Separator();
    }
    return state.expanded;
}

void chrome::end_panel() { ImGui::End(); }
