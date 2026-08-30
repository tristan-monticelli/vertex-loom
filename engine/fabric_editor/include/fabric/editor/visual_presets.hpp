#pragma once

#include "fabric/project/textured_path.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/project/visual_component.hpp"
#include "fabric/project/visual_composition.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::editor {

enum class VisualPresetKind { eye, button, seam, zipper };

[[nodiscard]] std::string_view label(VisualPresetKind kind) noexcept;

struct VisualPresetRequest {
    VisualPresetKind kind{VisualPresetKind::eye};
    core::ResourceId id{.value = "visual-preset"};
    std::string name{"Visual preset"};
    std::optional<project::ResourceReference> thread_texture;
    std::size_t zipper_tooth_count{12U};
};

struct VisualPresetBundle {
    std::vector<project::VectorAsset> vectors;
    std::vector<project::TexturedPath> textured_paths;
    project::VisualComposition composition;
    project::VisualComponent component;

    friend bool operator==(const VisualPresetBundle&,
                           const VisualPresetBundle&) = default;
};

struct VisualPresetResult {
    std::optional<VisualPresetBundle> bundle;
    std::vector<project::Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return bundle.has_value() && errors.empty();
    }
};

[[nodiscard]] VisualPresetResult build_visual_preset(
    const project::ProjectManifest&, const VisualPresetRequest&);
[[nodiscard]] VisualPresetResult publish_visual_preset(
    const std::filesystem::path&, const project::ProjectManifest&,
    const VisualPresetRequest&);

} // namespace fabric::editor
