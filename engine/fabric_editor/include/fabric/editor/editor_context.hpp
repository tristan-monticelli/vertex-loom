#pragma once

#include "fabric/core/resource_id.hpp"
#include "fabric/core/types.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fabric::editor {

enum class EditorWorkspace {
    visual,
    entity,
    animation,
    logic,
    map,
    scene,
    rig_physics,
    publish,
};

struct EditorLocation {
    core::ResourceId document_id;
    EditorWorkspace workspace{EditorWorkspace::visual};
    std::optional<core::ResourceId> selection_id;
    std::vector<core::ResourceId> selection_ids;

    friend bool operator==(const EditorLocation&, const EditorLocation&) =
        default;
};

struct EditorViewState {
    float zoom{1.0F};
    core::Vec2 pan;
    float playhead{};
    std::string active_tool;
    std::string active_panel;

    friend bool operator==(const EditorViewState&, const EditorViewState&) =
        default;
};

struct EditorDocumentState {
    core::ResourceId id;
    EditorWorkspace workspace{EditorWorkspace::visual};
    std::optional<core::ResourceId> selection_id;
    std::vector<core::ResourceId> selection_ids;
    EditorViewState view;
};

struct ResolvedEditorSelection {
    std::optional<std::size_t> primary_index;
    std::vector<std::size_t> indices;
};

class EditorContext {
public:
    [[nodiscard]] bool open_document(
        const core::ResourceId& id,
        EditorWorkspace workspace = EditorWorkspace::visual) {
        const auto existing = find_document(id);
        return navigate(
            id, workspace,
            existing == documents_.end() ? std::nullopt
                                         : existing->selection_id,
            existing == documents_.end()
                ? std::vector<core::ResourceId>{}
                : existing->selection_ids);
    }

    [[nodiscard]] bool navigate(
        const core::ResourceId& document_id,
        EditorWorkspace workspace,
        std::optional<core::ResourceId> selection_id = std::nullopt,
        std::vector<core::ResourceId> selection_ids = {}) {
        if (!core::ResourceId::is_valid(document_id.value) ||
            (selection_id.has_value() &&
             !core::ResourceId::is_valid(selection_id->value)) ||
            std::ranges::any_of(selection_ids, [](const auto& id) {
                return !core::ResourceId::is_valid(id.value);
            })) {
            return false;
        }
        normalize_selection(selection_id, selection_ids);

        auto document = find_document(document_id);
        if (document == documents_.end()) {
            documents_.push_back(EditorDocumentState{
                .id = document_id,
                .workspace = workspace,
                .selection_id = selection_id,
                .selection_ids = selection_ids,
            });
            document = std::prev(documents_.end());
        } else {
            document->workspace = workspace;
            document->selection_id = selection_id;
            document->selection_ids = selection_ids;
        }

        const EditorLocation next{
            .document_id = document_id,
            .workspace = workspace,
            .selection_id = std::move(selection_id),
            .selection_ids = std::move(selection_ids),
        };
        active_document_ = document_index(document_id);
        if (!history_.empty() && history_[history_cursor_] == next) {
            return true;
        }
        if (!history_.empty()) {
            history_.erase(history_.begin() +
                               static_cast<std::ptrdiff_t>(history_cursor_ + 1),
                           history_.end());
        }
        history_.push_back(next);
        history_cursor_ = history_.size() - 1;
        return true;
    }

    [[nodiscard]] bool close_document(const core::ResourceId& id) {
        const auto document = find_document(id);
        if (document == documents_.end()) {
            return false;
        }

        const auto closed_index = static_cast<std::size_t>(
            std::distance(documents_.begin(), document));
        std::optional<core::ResourceId> preserved_active_id;
        if (active_document_.has_value() &&
            documents_[*active_document_].id != id) {
            preserved_active_id = documents_[*active_document_].id;
        }
        documents_.erase(document);
        history_.erase(
            std::remove_if(history_.begin(), history_.end(),
                           [&id](const EditorLocation& location) {
                               return location.document_id == id;
                           }),
            history_.end());

        if (documents_.empty()) {
            active_document_.reset();
            history_cursor_ = 0;
            return true;
        }

        if (preserved_active_id.has_value()) {
            active_document_ = document_index(*preserved_active_id);
        } else {
            active_document_ = std::min(closed_index, documents_.size() - 1);
        }
        const auto& active = documents_[*active_document_];
        history_.clear();
        history_.push_back(EditorLocation{
            .document_id = active.id,
            .workspace = active.workspace,
            .selection_id = active.selection_id,
            .selection_ids = active.selection_ids,
        });
        history_cursor_ = 0;
        return true;
    }

    [[nodiscard]] bool set_selection(
        std::optional<core::ResourceId> selection_id) {
        std::vector<core::ResourceId> selection_ids;
        if (selection_id.has_value()) selection_ids.push_back(*selection_id);
        return set_selection_set(std::move(selection_id),
                                 std::move(selection_ids));
    }

    [[nodiscard]] bool set_selection_set(
        std::optional<core::ResourceId> primary_id,
        std::vector<core::ResourceId> selection_ids) {
        if (!active_document_.has_value() ||
            (primary_id.has_value() &&
             !core::ResourceId::is_valid(primary_id->value)) ||
            std::ranges::any_of(selection_ids, [](const auto& id) {
                return !core::ResourceId::is_valid(id.value);
            })) {
            return false;
        }
        normalize_selection(primary_id, selection_ids);
        auto& document = documents_[*active_document_];
        document.selection_id = primary_id;
        document.selection_ids = selection_ids;
        if (!history_.empty()) {
            history_[history_cursor_].selection_id = std::move(primary_id);
            history_[history_cursor_].selection_ids = std::move(selection_ids);
        }
        return true;
    }

    [[nodiscard]] bool set_view(EditorViewState view) {
        if (!active_document_.has_value()) {
            return false;
        }
        documents_[*active_document_].view = std::move(view);
        return true;
    }

    [[nodiscard]] bool go_back() {
        if (!can_go_back()) {
            return false;
        }
        --history_cursor_;
        apply(history_[history_cursor_]);
        return true;
    }

    [[nodiscard]] bool go_forward() {
        if (!can_go_forward()) {
            return false;
        }
        ++history_cursor_;
        apply(history_[history_cursor_]);
        return true;
    }

    [[nodiscard]] bool can_go_back() const noexcept {
        return !history_.empty() && history_cursor_ > 0;
    }

    [[nodiscard]] bool can_go_forward() const noexcept {
        return !history_.empty() && history_cursor_ + 1 < history_.size();
    }

    [[nodiscard]] const EditorDocumentState* active_document() const noexcept {
        if (!active_document_.has_value()) {
            return nullptr;
        }
        return &documents_[*active_document_];
    }

    [[nodiscard]] const std::vector<EditorDocumentState>& open_documents()
        const noexcept {
        return documents_;
    }

    [[nodiscard]] ResolvedEditorSelection resolve_selection(
        const std::vector<core::ResourceId>& available_ids) const {
        ResolvedEditorSelection resolved;
        if (!active_document_.has_value()) return resolved;
        const auto& document = documents_[*active_document_];
        const auto resolve = [&](const core::ResourceId& id)
            -> std::optional<std::size_t> {
            const auto found = std::ranges::find(available_ids, id);
            if (found == available_ids.end()) return std::nullopt;
            return static_cast<std::size_t>(
                std::distance(available_ids.begin(), found));
        };
        for (const auto& id : document.selection_ids) {
            if (const auto index = resolve(id); index.has_value())
                resolved.indices.push_back(*index);
        }
        if (document.selection_id.has_value())
            resolved.primary_index = resolve(*document.selection_id);
        if (!resolved.primary_index.has_value() && !resolved.indices.empty())
            resolved.primary_index = resolved.indices.front();
        return resolved;
    }

private:
    using DocumentIterator = std::vector<EditorDocumentState>::iterator;

    [[nodiscard]] DocumentIterator find_document(const core::ResourceId& id) {
        return std::find_if(documents_.begin(), documents_.end(),
                            [&id](const EditorDocumentState& document) {
                                return document.id == id;
                            });
    }

    [[nodiscard]] std::size_t document_index(const core::ResourceId& id) {
        return static_cast<std::size_t>(
            std::distance(documents_.begin(), find_document(id)));
    }

    static void normalize_selection(
        std::optional<core::ResourceId>& primary_id,
        std::vector<core::ResourceId>& selection_ids) {
        std::vector<core::ResourceId> unique;
        unique.reserve(selection_ids.size() + (primary_id.has_value() ? 1U : 0U));
        for (auto& id : selection_ids) {
            if (std::ranges::find(unique, id) == unique.end())
                unique.push_back(std::move(id));
        }
        if (!primary_id.has_value() && !unique.empty())
            primary_id = unique.front();
        if (primary_id.has_value()) {
            const auto primary = std::ranges::find(unique, *primary_id);
            if (primary != unique.end()) unique.erase(primary);
            unique.insert(unique.begin(), *primary_id);
        }
        selection_ids = std::move(unique);
    }

    void apply(const EditorLocation& location) {
        auto document = find_document(location.document_id);
        if (document == documents_.end()) {
            return;
        }
        document->workspace = location.workspace;
        document->selection_id = location.selection_id;
        document->selection_ids = location.selection_ids;
        active_document_ = document_index(location.document_id);
    }

    std::vector<EditorDocumentState> documents_;
    std::optional<std::size_t> active_document_;
    std::vector<EditorLocation> history_;
    std::size_t history_cursor_{};
};

} // namespace fabric::editor
