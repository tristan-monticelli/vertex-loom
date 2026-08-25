#pragma once

#include "fabric/project/input.hpp"

#include <string>
#include <string_view>
#include <span>
#include <unordered_set>
#include <vector>

namespace fabric::runtime {

using InputDevice = project::InputDevice;
using InputBinding = project::InputBinding;
using InputActionDefinition = project::InputActionDefinition;

class InputActionMap {
public:
    [[nodiscard]] bool define_action(std::string id);
    [[nodiscard]] bool bind(std::string_view action, InputBinding binding);
    [[nodiscard]] bool configure(std::span<const InputActionDefinition> definitions);
    void begin_frame() noexcept;
    void press(InputDevice device, int code, bool repeat = false) noexcept;
    void release(InputDevice device, int code) noexcept;
    void press_action(std::string_view action) noexcept;
    void release_action(std::string_view action) noexcept;

    [[nodiscard]] bool held(std::string_view action) const noexcept;
    [[nodiscard]] bool pressed(std::string_view action) const noexcept;
    [[nodiscard]] bool released(std::string_view action) const noexcept;
    [[nodiscard]] const std::vector<InputActionDefinition>& actions() const noexcept {
        return actions_;
    }

private:
    [[nodiscard]] bool matches(std::string_view action, InputDevice device,
                               int code) const noexcept;
    [[nodiscard]] std::string key(InputDevice device, int code) const;

    std::vector<InputActionDefinition> actions_;
    std::unordered_set<std::string> held_bindings_;
    std::unordered_set<std::string> pressed_bindings_;
    std::unordered_set<std::string> released_bindings_;
    std::unordered_set<std::string> held_actions_;
    std::unordered_set<std::string> pressed_actions_;
    std::unordered_set<std::string> released_actions_;
};

} // namespace fabric::runtime
