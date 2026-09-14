#include "chrome/chrome.hpp"
#include "chrome/footer.hpp"
#include "chrome/viewport.hpp"
#include "chrome/simulation.hpp"
#include "chrome/rendering.hpp"
#include <imgui.h>
#include <algorithm>

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

chrome::ViewportState chrome::Chrome::draw_layout(TextureHandle viewport_texture, std::string_view status) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 area = viewport->WorkSize;
    if (area.x <= 0 || area.y <= 0) return {};
    const float footer = std::min(footer_height(), area.y);
    const float body = std::max(0.0f, area.y - footer);
    const float rail = std::min(ImGui::GetFrameHeight() + 4, area.x / 3);
    float left = simulation_.expanded ? std::clamp(area.x * 0.20f, 180.0f, 320.0f) : rail;
    float right = rendering_.expanded ? std::clamp(area.x * 0.24f, 220.0f, 380.0f) : rail;
    // Reserve viewport space, then shrink the expanded portions of the panels.
    const float minimum = std::min(rail, area.x / 3);
    const float budget = std::max(2 * minimum, area.x - std::min(160.0f, area.x / 3));
    if (left + right > budget) {
        const float extra = left + right - 2 * minimum;
        const float factor = extra > 0 ? (budget - 2 * minimum) / extra : 0;
        left = minimum + (left - minimum) * factor;
        right = minimum + (right - minimum) * factor;
    }
    // Expanded panels overlay the same image instead of resizing or recentering it.
    // Submit the viewport first so newly created panels appear above it.
    const auto state = draw_viewport({origin.x + rail, origin.y,
                                     std::max(0.0f, area.x - 2 * rail), body}, viewport_texture);
    if (body > 0) {
        draw_simulation({origin.x, origin.y, left, body}, simulation_);
        draw_rendering({origin.x + area.x - right, origin.y, right, body}, rendering_, display_field_);
    }
    draw_footer({origin.x, origin.y + body, area.x, footer}, status);
    return state;
}

chrome::DrawData chrome::Chrome::finalize_frame() {
    ImGui::Render();
    return {ImGui::GetDrawData()};
}
