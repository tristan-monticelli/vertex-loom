#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace fabric::editor {

class Command {
public:
    virtual ~Command() = default;

    [[nodiscard]] virtual bool execute() = 0;
    [[nodiscard]] virtual bool undo() = 0;
    [[nodiscard]] virtual bool merge_with(const Command& newer);
};

class CommandStack {
public:
    [[nodiscard]] bool execute(std::unique_ptr<Command> command);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    void mark_clean() noexcept;
    void mark_dirty() noexcept;
    void clear() noexcept;

private:
    std::vector<std::unique_ptr<Command>> history_;
    std::size_t cursor_{};
    std::optional<std::size_t> clean_cursor_{0};
};

} // namespace fabric::editor
