#include "chrome/footer.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>

float chrome::footer_height() {
    return ImGui::GetTextLineHeight() + 2 * ImGui::GetStyle().FramePadding.y;
}

void chrome::draw_footer(Bounds bounds, std::string_view message) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    ImGui::SetNextWindowPos({bounds.x, bounds.y});
    ImGui::SetNextWindowSize({bounds.width, bounds.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {1, 1});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().FramePadding);
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##footer", nullptr, flags)) {
        std::string text(message.empty() ? "Ready" : message);
        for (char& c : text) if (c == '\n' || c == '\r') c = ' ';
        const float available = std::max(0.0f, ImGui::GetContentRegionAvail().x);
        const bool truncated = ImGui::CalcTextSize(text.c_str()).x > available;
        if (truncated) {
            const float suffix_width = ImGui::CalcTextSize("...").x;
            while (!text.empty() && ImGui::CalcTextSize(text.c_str()).x + suffix_width > available) {
                // Remove an entire UTF-8 code point.
                size_t end = text.size() - 1;
                while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
                text.resize(end);
            }
            text += "...";
        }
        ImGui::TextUnformatted(text.c_str());
        if (truncated && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40);
            ImGui::TextUnformatted(message.data(), message.data() + message.size());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
