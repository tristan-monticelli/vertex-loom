#include "fabric/editor/command_stack.hpp"

namespace fabric::editor {

bool Command::merge_with(const Command&) {
    return false;
}

bool CommandStack::execute(std::unique_ptr<Command> command) {
    if (!command || !command->execute()) {
        return false;
    }

    if (cursor_ < history_.size()) {
        if (clean_cursor_.has_value() && *clean_cursor_ > cursor_) {
            clean_cursor_.reset();
        }
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                       history_.end());
    }

    const bool may_merge = cursor_ > 0 &&
        (!clean_cursor_.has_value() || *clean_cursor_ != cursor_);
    if (may_merge && history_[cursor_ - 1]->merge_with(*command)) {
        return true;
    }

    history_.push_back(std::move(command));
    ++cursor_;
    return true;
}

bool CommandStack::undo() {
    if (!can_undo() || !history_[cursor_ - 1]->undo()) {
        return false;
    }
    --cursor_;
    return true;
}

bool CommandStack::redo() {
    if (!can_redo() || !history_[cursor_]->execute()) {
        return false;
    }
    ++cursor_;
    return true;
}

bool CommandStack::can_undo() const noexcept {
    return cursor_ > 0;
}

bool CommandStack::can_redo() const noexcept {
    return cursor_ < history_.size();
}

bool CommandStack::dirty() const noexcept {
    return !clean_cursor_.has_value() || cursor_ != *clean_cursor_;
}

std::size_t CommandStack::size() const noexcept {
    return history_.size();
}

void CommandStack::mark_clean() noexcept {
    clean_cursor_ = cursor_;
}

void CommandStack::mark_dirty() noexcept {
    clean_cursor_.reset();
}

void CommandStack::clear() noexcept {
    history_.clear();
    cursor_ = 0;
    clean_cursor_ = 0;
}

} // namespace fabric::editor
