#pragma once

#include "fabric/editor/editor_context.hpp"
#include "fabric/editor/editor_action_registry.hpp"

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

inline const char* workspace_label(
    const editor::EditorWorkspace workspace) noexcept {
    switch (workspace) {
    case editor::EditorWorkspace::visual: return "Visual";
    case editor::EditorWorkspace::entity: return "Entity";
    case editor::EditorWorkspace::animation: return "Animation";
    case editor::EditorWorkspace::logic: return "Logic";
    case editor::EditorWorkspace::map: return "Map";
    case editor::EditorWorkspace::scene: return "Scene";
    case editor::EditorWorkspace::rig_physics: return "Rig / Physics";
    case editor::EditorWorkspace::publish: return "Publish";
    }
    return "Document";
}

template <typename LabelFor, typename Activate>
bool draw_document_navigation(editor::EditorContext& context,
                              LabelFor&& label_for,
                              Activate&& activate) {
    bool changed = false;
    ImGui::BeginDisabled(!context.can_go_back());
    if (ImGui::ArrowButton("##document-back", ImGuiDir_Left) &&
        context.go_back() && context.active_document() != nullptr) {
        changed = activate(*context.active_document());
    }
    ImGui::EndDisabled();
    draw_disabled_reason(!context.can_go_back(),
                         "No previous document in navigation history.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.can_go_forward());
    if (ImGui::ArrowButton("##document-forward", ImGuiDir_Right) &&
        context.go_forward() && context.active_document() != nullptr) {
        changed = activate(*context.active_document()) || changed;
    }
    ImGui::EndDisabled();
    draw_disabled_reason(!context.can_go_forward(),
                         "No next document in navigation history.");
    ImGui::SameLine();
    const auto* active = context.active_document();
    ImGui::TextDisabled("%s", active == nullptr
        ? "No document" : workspace_label(active->workspace));

    if (ImGui::BeginTabBar(
            "##document-tabs",
            ImGuiTabBarFlags_Reorderable |
                ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (const auto& document : context.open_documents()) {
            const bool is_active = active != nullptr &&
                active->id == document.id;
            const auto visible_label = label_for(document.id);
            const std::string tab_label =
                (visible_label.empty() ? document.id.value : visible_label) +
                "##document-" + document.id.value;
            const auto flags = is_active ? ImGuiTabItemFlags_SetSelected
                                         : ImGuiTabItemFlags_None;
            const bool tab_visible =
                ImGui::BeginTabItem(tab_label.c_str(), nullptr, flags);
            const bool tab_clicked = ImGui::IsItemClicked();
            if (tab_clicked && !is_active &&
                context.navigate(document.id, document.workspace,
                                 document.selection_id)) {
                changed = activate(*context.active_document()) || changed;
                active = context.active_document();
            }
            if (tab_visible) {
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    return changed;
}

inline bool draw_command_palette(editor::EditorActionRegistry& registry,
                                 bool& open) {
    if (open) {
        ImGui::OpenPopup("Command Palette");
        open = false;
    }
    ImGui::SetNextWindowSize({520.0F, 360.0F}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Command Palette", nullptr,
                                ImGuiWindowFlags_NoSavedSettings)) {
        return false;
    }

    static std::string filter;
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputTextWithHint("##command-search", "Search commands...",
                             &filter);
    ImGui::Separator();
    bool found = false;
    for (const auto& action : registry.actions()) {
        if (!contains_ascii_insensitive(action.label, filter) &&
            !contains_ascii_insensitive(action.id, filter) &&
            !contains_ascii_insensitive(action.shortcut, filter)) {
            continue;
        }
        found = true;
        const auto state = registry.availability(action.id);
        ImGui::BeginDisabled(!state.enabled);
        const std::string label = action.shortcut.empty()
            ? action.label + "##command-" + action.id
            : action.label + "\t" + action.shortcut + "##command-" +
                action.id;
        if (ImGui::Selectable(label.c_str())) {
            static_cast<void>(registry.invoke(action.id));
            filter.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!state.enabled, state.disabled_reason);
    }
    if (!found) {
        ImGui::TextDisabled("No matching command.");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        filter.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return true;
}

} // namespace fabric::editor_ui
