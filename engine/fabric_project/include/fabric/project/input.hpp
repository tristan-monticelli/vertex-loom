#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_input_schema_version = 2;

enum class InputDevice { keyboard, gamepad };
enum class InputBindingKind { button, axis };

struct InputBinding {
    InputDevice device{InputDevice::keyboard};
    int code{};
    InputBindingKind kind{InputBindingKind::button};
    float threshold{0.5F};
    float dead_zone{0.1F};

    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct InputActionDefinition {
    std::string id;
    std::vector<InputBinding> bindings;

    friend bool operator==(const InputActionDefinition&,
                           const InputActionDefinition&) = default;
};

struct InputDocument {
    DocumentHeader document{
        .schema_version = current_input_schema_version,
        .type = "input",
        .id = {},
        .name = "Input"};
    std::vector<InputActionDefinition> actions;

    friend bool operator==(const InputDocument&, const InputDocument&) = default;
};

struct InputResult {
    std::optional<InputDocument> input;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return input.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path input_document_path(
    const ProjectManifest&, const core::ResourceId& id);
[[nodiscard]] ValidationReport validate_input(
    const ProjectManifest&, const InputDocument&);
[[nodiscard]] InputResult parse_input(std::string_view serialized);
[[nodiscard]] std::string serialize_input(const InputDocument&);
[[nodiscard]] InputResult load_input(const std::filesystem::path& project_root,
                                     const ProjectManifest&,
                                     const std::filesystem::path& document_path);
[[nodiscard]] InputResult publish_input(const std::filesystem::path& project_root,
                                        const ProjectManifest&,
                                        const InputDocument&);

} // namespace fabric::project
