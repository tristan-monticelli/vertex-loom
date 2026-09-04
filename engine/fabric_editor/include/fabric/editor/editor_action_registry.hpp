#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fabric::editor {

namespace editor_action_ids {
inline constexpr std::string_view new_project = "new_project";
inline constexpr std::string_view open_project = "open_project";
inline constexpr std::string_view save = "save";
inline constexpr std::string_view preview = "preview";
inline constexpr std::string_view validate = "validate";
inline constexpr std::string_view publish = "publish";
inline constexpr std::string_view undo = "undo";
inline constexpr std::string_view redo = "redo";
inline constexpr std::string_view navigate_back = "navigate_back";
inline constexpr std::string_view navigate_forward = "navigate_forward";
inline constexpr std::string_view create_entity_from_visuals =
    "create_entity_from_visuals";
inline constexpr std::string_view animate_selection = "animate_selection";
inline constexpr std::string_view toggle_animation_graph =
    "toggle_animation_graph";
} // namespace editor_action_ids

struct EditorActionAvailability {
    bool enabled{true};
    std::string disabled_reason;
};

struct EditorActionDefinition {
    std::string id;
    std::string label;
    std::string shortcut;
    std::function<EditorActionAvailability()> availability;
    std::function<bool()> execute;
};

enum class EditorActionInvocation {
    invoked,
    failed,
    disabled,
    unknown,
};

class EditorActionRegistry {
public:
    [[nodiscard]] bool register_action(EditorActionDefinition definition) {
        if (definition.id.empty() || !definition.execute ||
            find(definition.id) != actions_.end()) {
            return false;
        }
        actions_.push_back(std::move(definition));
        return true;
    }

    [[nodiscard]] const EditorActionDefinition* action(
        std::string_view id) const noexcept {
        const auto result = find(id);
        return result == actions_.end() ? nullptr : &*result;
    }

    [[nodiscard]] EditorActionAvailability availability(
        std::string_view id) const {
        const auto* definition = action(id);
        if (definition == nullptr) {
            return {.enabled = false, .disabled_reason = "Action inconnue"};
        }
        if (!definition->availability) {
            return {};
        }
        auto state = definition->availability();
        if (state.enabled) {
            state.disabled_reason.clear();
        } else if (state.disabled_reason.empty()) {
            state.disabled_reason = "Action indisponible dans ce contexte";
        }
        return state;
    }

    [[nodiscard]] EditorActionInvocation invoke(std::string_view id) const {
        const auto* definition = action(id);
        if (definition == nullptr) {
            return EditorActionInvocation::unknown;
        }
        if (!availability(id).enabled) {
            return EditorActionInvocation::disabled;
        }
        return definition->execute() ? EditorActionInvocation::invoked
                                     : EditorActionInvocation::failed;
    }

    [[nodiscard]] const std::vector<EditorActionDefinition>& actions()
        const noexcept {
        return actions_;
    }

private:
    using ConstIterator = std::vector<EditorActionDefinition>::const_iterator;

    [[nodiscard]] ConstIterator find(std::string_view id) const noexcept {
        return std::find_if(actions_.begin(), actions_.end(),
                            [id](const EditorActionDefinition& action) {
                                return action.id == id;
                            });
    }

    std::vector<EditorActionDefinition> actions_;
};

} // namespace fabric::editor
