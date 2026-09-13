#include "chrome/viewport.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

chrome::ViewportState chrome::draw_viewport(Bounds bounds, TextureHandle texture) {
    if (bounds.width <= 0 || bounds.height <= 0) return {};
    ImGui::SetNextWindowPos({bounds.x, bounds.y});
    ImGui::SetNextWindowSize({bounds.width, bounds.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {1, 1});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool visible = ImGui::Begin("##viewport", nullptr, flags);
    ImGui::PopStyleVar(3);
    const ImVec2 content = ImGui::GetContentRegionAvail();
    ImVec2 orbit_delta{};
    if (visible && content.x > 0 && content.y > 0) {
        const ImVec2 position = ImGui::GetCursorScreenPos();
        if (texture.native) ImGui::Image(static_cast<ImTextureID>(texture.native), content);
        ImGui::SetCursorScreenPos(position);
        // An active item keeps ownership of a drag after it leaves the viewport,
        // while clicks originating on overlaid panels cannot start an orbit.
        ImGui::InvisibleButton("orbit", content, ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            orbit_delta = ImGui::GetIO().MouseDelta;
    }
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const ViewportState state{
        visible ? std::max(0, static_cast<int>(std::ceil(content.x * scale.x))) : 0,
        visible ? std::max(0, static_cast<int>(std::ceil(content.y * scale.y))) : 0,
        ImGui::IsWindowHovered(), ImGui::IsWindowFocused(), orbit_delta.x, orbit_delta.y};
    ImGui::End();
    return state;
}
