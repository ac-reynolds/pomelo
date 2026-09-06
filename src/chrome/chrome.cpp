#include "chrome/chrome.hpp"
#include <imgui.h>

#include <algorithm>

namespace {

void empty_panel(const char* name, ImVec2 position, ImVec2 size) {
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(name, nullptr, flags);
    ImGui::End();
}

} // namespace

chrome::Chrome::Chrome() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

chrome::Chrome::~Chrome() {
    ImGui::DestroyContext();
}

void chrome::Chrome::begin_frame() {
    ImGui::NewFrame();
}

chrome::ViewportState chrome::Chrome::draw_layout(TextureHandle viewport_texture) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 area = viewport->WorkSize;
    constexpr float footer_height = 90.0f;
    const float body_height = std::max(1.0f, area.y - footer_height);
    const float left_width = std::clamp(area.x * 0.20f, 180.0f, 320.0f);
    const float right_width = std::clamp(area.x * 0.24f, 220.0f, 380.0f);
    empty_panel("Left", origin, {left_width, body_height});
    const ImVec2 viewport_size{std::max(1.0f, area.x - left_width - right_width), body_height};
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos({origin.x + left_width, origin.y});
    ImGui::SetNextWindowSize(viewport_size);
    ImGui::Begin("Viewport", nullptr, flags);
    const ImVec2 content = ImGui::GetContentRegionAvail();
    if (viewport_texture.native)
        ImGui::Image(static_cast<ImTextureID>(viewport_texture.native), content);
    const ViewportState state{std::max(1, static_cast<int>(content.x)),
                              std::max(1, static_cast<int>(content.y)),
                              ImGui::IsWindowHovered(), ImGui::IsWindowFocused()};
    ImGui::End();
    empty_panel("Right", {origin.x + area.x - right_width, origin.y}, {right_width, body_height});
    empty_panel("Footer", {origin.x, origin.y + body_height}, {area.x, footer_height});
    return state;
}

chrome::DrawData chrome::Chrome::finalize_frame() {
    ImGui::Render();
    return {ImGui::GetDrawData()};
}
