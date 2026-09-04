#pragma once

#include <imgui.h>
#include <imgui_stdlib.h>

#include <string>
#include <string_view>

namespace fabric::editor_ui {

inline void draw_disabled_reason(const bool disabled,
                                 const std::string_view reason) {
    if (!disabled ||
        !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    ImGui::SetTooltip("%s", std::string(reason).c_str());
}

inline void draw_technical_tooltip(const std::string_view text) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", std::string(text).c_str());
    }
}

inline bool draw_resource_name_field(
    const char* label,
    std::string& name,
    const float width = 560.0F,
    const ImGuiInputTextFlags flags = 0) {
    ImGui::SetNextItemWidth(width);
    return ImGui::InputText(label, &name, flags);
}

} // namespace fabric::editor_ui
