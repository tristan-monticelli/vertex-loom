#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fabric::runtime {

enum class InputDevice { keyboard, gamepad };

struct InputBinding {
    InputDevice device{InputDevice::keyboard};
    int code{};
    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct InputActionDefinition {
    std::string id;
    std::vector<InputBinding> bindings;
    friend bool operator==(const InputActionDefinition&, const InputActionDefinition&) = default;
};

class InputActionMap {
public:
    [[nodiscard]] bool define_action(std::string id);
    [[nodiscard]] bool bind(std::string_view action, InputBinding binding);
    void begin_frame() noexcept;
    void press(InputDevice device, int code, bool repeat = false) noexcept;
    void release(InputDevice device, int code) noexcept;

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
};

} // namespace fabric::runtime
