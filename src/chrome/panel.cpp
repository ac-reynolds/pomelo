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
    const ImVec2 header = ImGui::GetCursorScreenPos();
    const float button_size = ImGui::GetFrameHeight();
    const float button_x = std::max(header.x, bounds.x + bounds.width - 2 - button_size);
    if (right) ImGui::SetCursorScreenPos({button_x, header.y});
    if (ImGui::Button(arrow, {button_size, button_size})) state.expanded = !state.expanded;
    ImGui::SetItemTooltip("%s %s", state.expanded ? "Collapse" : "Expand", name);
    if (state.expanded) {
        if (right) {
            // Clip the label before the fixed toggle on narrow panels.
            ImGui::GetWindowDrawList()->PushClipRect(header,
                {std::max(header.x, button_x - ImGui::GetStyle().ItemSpacing.x), header.y + button_size}, true);
            ImGui::GetWindowDrawList()->AddText(
                {header.x, header.y + ImGui::GetStyle().FramePadding.y},
                ImGui::GetColorU32(ImGuiCol_Text), name);
            ImGui::GetWindowDrawList()->PopClipRect();
        } else {
            ImGui::SameLine();
            ImGui::TextUnformatted(name);
        }
        ImGui::Separator();
    }
    return state.expanded;
}

void chrome::end_panel() { ImGui::End(); }
