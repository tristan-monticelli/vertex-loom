#include "fabric/runtime/input.hpp"

#include <string_view>
#include <utility>

namespace fabric::runtime {

bool InputActionMap::define_action(std::string id) {
    if (id.empty()) return false;
    for (const auto& action : actions_)
        if (action.id == id) return false;
    actions_.push_back({std::move(id), {}});
    return true;
}

bool InputActionMap::bind(const std::string_view action, const InputBinding binding) {
    if (binding.code < 0) return false;
    for (auto& definition : actions_) {
        if (definition.id != action) continue;
        for (const auto existing : definition.bindings)
            if (existing == binding) return false;
        definition.bindings.push_back(binding);
        return true;
    }
    return false;
}

std::string InputActionMap::key(const InputDevice device, const int code) const {
    return std::to_string(static_cast<int>(device)) + ":" + std::to_string(code);
}

bool InputActionMap::matches(const std::string_view action, const InputDevice device,
                             const int code) const noexcept {
    for (const auto& definition : actions_) {
        if (definition.id != action) continue;
        for (const auto binding : definition.bindings)
            if (binding.device == device && binding.code == code) return true;
        return false;
    }
    return false;
}

void InputActionMap::begin_frame() noexcept {
    pressed_bindings_.clear();
    released_bindings_.clear();
    pressed_actions_.clear();
    released_actions_.clear();
}

void InputActionMap::press(const InputDevice device, const int code, const bool repeat) noexcept {
    const auto binding = key(device, code);
    held_bindings_.insert(binding);
    if (!repeat) pressed_bindings_.insert(binding);
}

void InputActionMap::release(const InputDevice device, const int code) noexcept {
    const auto binding = key(device, code);
    if (held_bindings_.erase(binding) != 0U) released_bindings_.insert(binding);
}

void InputActionMap::press_action(const std::string_view action) noexcept {
    if (action.empty()) return;
    const std::string value(action);
    held_actions_.insert(value);
    pressed_actions_.insert(value);
}

void InputActionMap::release_action(const std::string_view action) noexcept {
    if (action.empty()) return;
    const std::string value(action);
    if (held_actions_.erase(value) != 0U) released_actions_.insert(value);
}

bool InputActionMap::held(const std::string_view action) const noexcept {
    if (held_actions_.contains(std::string(action))) return true;
    for (const auto& binding : held_bindings_) {
        const auto separator = binding.find(':');
        if (separator == std::string::npos) continue;
        const auto device = static_cast<InputDevice>(std::stoi(binding.substr(0, separator)));
        const auto code = std::stoi(binding.substr(separator + 1));
        if (matches(action, device, code)) return true;
    }
    return false;
}

bool InputActionMap::pressed(const std::string_view action) const noexcept {
    if (pressed_actions_.contains(std::string(action))) return true;
    for (const auto& binding : pressed_bindings_) {
        const auto separator = binding.find(':');
        if (separator == std::string::npos) continue;
        if (matches(action,
                    static_cast<InputDevice>(std::stoi(binding.substr(0, separator))),
                    std::stoi(binding.substr(separator + 1)))) return true;
    }
    return false;
}

bool InputActionMap::released(const std::string_view action) const noexcept {
    if (released_actions_.contains(std::string(action))) return true;
    for (const auto& binding : released_bindings_) {
        const auto separator = binding.find(':');
        if (separator == std::string::npos) continue;
        if (matches(action,
                    static_cast<InputDevice>(std::stoi(binding.substr(0, separator))),
                    std::stoi(binding.substr(separator + 1)))) return true;
    }
    return false;
}

} // namespace fabric::runtime
