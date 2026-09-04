#pragma once

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

struct SearchableIdOption {
    std::string id;
    std::string label;
    std::string detail;
};

struct SearchableIdPickerOptions {
    float width{220.0F};
    bool allow_clear{};
    std::string_view empty_label{"Choose an identifier..."};
    std::string_view search_hint{"Search by name or id..."};
    std::string_view no_matches_label{"No matching identifier."};
};

inline bool contains_ascii_insensitive(const std::string_view text,
                                       const std::string_view query) {
    if (query.empty()) {
        return true;
    }
    const auto fold = [](const char value) {
        return value >= 'A' && value <= 'Z'
            ? static_cast<char>(value - 'A' + 'a')
            : value;
    };
    for (std::size_t start = 0; start + query.size() <= text.size(); ++start) {
        bool matches = true;
        for (std::size_t offset = 0; offset < query.size(); ++offset) {
            if (fold(text[start + offset]) != fold(query[offset])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

inline bool draw_searchable_id_picker(
    const char* label,
    const std::span<const SearchableIdOption> options,
    std::string& selected_id,
    const SearchableIdPickerOptions& settings = {}) {
    const auto selected = std::ranges::find(options, selected_id,
                                            &SearchableIdOption::id);
    const std::string preview = selected != options.end()
        ? selected->label
        : selected_id.empty() ? std::string{settings.empty_label}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(settings.width);
    if (!ImGui::BeginCombo(label, preview.c_str())) {
        return false;
    }

    static std::unordered_map<ImGuiID, std::string> filters;
    auto& filter = filters[ImGui::GetID(label)];
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##searchable-id-picker",
                             std::string(settings.search_hint).c_str(),
                             &filter);
    if (settings.allow_clear &&
        ImGui::Selectable("Clear selection", selected_id.empty())) {
        selected_id.clear();
        changed = true;
    }
    if (settings.allow_clear) {
        ImGui::Separator();
    }

    bool found = false;
    for (const auto& option : options) {
        if (!contains_ascii_insensitive(option.label, filter) &&
            !contains_ascii_insensitive(option.id, filter) &&
            !contains_ascii_insensitive(option.detail, filter)) {
            continue;
        }
        found = true;
        const bool is_selected = option.id == selected_id;
        const std::string item_label = option.label + "##searchable-option-" +
            option.id;
        if (ImGui::Selectable(item_label.c_str(), is_selected)) {
            selected_id = option.id;
            changed = true;
        }
        if (is_selected) {
            ImGui::SetItemDefaultFocus();
        }
        if (!option.detail.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", option.detail.c_str());
        }
    }
    if (!found) {
        ImGui::TextDisabled("%s",
                            std::string(settings.no_matches_label).c_str());
    }
    ImGui::EndCombo();
    return changed;
}

} // namespace fabric::editor_ui
