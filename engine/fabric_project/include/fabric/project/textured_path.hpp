#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/shader_profile.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_textured_path_schema_version = 1;

enum class TexturedPathCommandKind { move, line, cubic };
enum class TexturedPathUvMode { repeat, stretch };
enum class TexturedPathJoin { miter, round, bevel };
enum class TexturedPathCap { butt, round, square };

[[nodiscard]] std::string_view to_string(
    TexturedPathCommandKind kind) noexcept;
[[nodiscard]] std::string_view to_string(TexturedPathUvMode mode) noexcept;
[[nodiscard]] std::string_view to_string(TexturedPathJoin join) noexcept;
[[nodiscard]] std::string_view to_string(TexturedPathCap cap) noexcept;

struct TexturedPathCommand {
    TexturedPathCommandKind kind{TexturedPathCommandKind::move};
    core::Vec2 point;
    core::Vec2 control1;
    core::Vec2 control2;

    friend bool operator==(const TexturedPathCommand&,
                           const TexturedPathCommand&) = default;
};

struct TexturedPathWidthKey {
    float position{};
    float width{1.0F};

    friend bool operator==(const TexturedPathWidthKey&,
                           const TexturedPathWidthKey&) = default;
};

struct TexturedPath {
    DocumentHeader document{
        .schema_version = current_textured_path_schema_version,
        .type = "texturedPath",
    };
    std::vector<TexturedPathCommand> commands;
    bool closed{};
    float width{1.0F};
    std::vector<TexturedPathWidthKey> width_profile;
    ResourceReference texture{{}, "texture"};
    TexturedPathUvMode uv_mode{TexturedPathUvMode::repeat};
    core::Vec2 uv_scale{1.0F, 1.0F};
    core::Vec2 uv_offset;
    core::Color color;
    float opacity{1.0F};
    TexturedPathJoin join{TexturedPathJoin::miter};
    TexturedPathCap cap{TexturedPathCap::butt};
    float miter_limit{4.0F};
    ShaderSurfaceSettings shader;

    friend bool operator==(const TexturedPath&, const TexturedPath&) = default;
};

struct TexturedPathResult {
    std::optional<TexturedPath> asset;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path textured_path_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_textured_path(
    const ProjectManifest&, const TexturedPath&);
[[nodiscard]] std::vector<ResourceReference> textured_path_resource_references(
    const TexturedPath&);
[[nodiscard]] std::string serialize_textured_path(const TexturedPath&);
[[nodiscard]] TexturedPathResult parse_textured_path(
    const ProjectManifest&, std::string_view);
[[nodiscard]] TexturedPathResult load_textured_path(
    const std::filesystem::path&, const ProjectManifest&,
    const std::filesystem::path&);
[[nodiscard]] TexturedPathResult publish_textured_path(
    const std::filesystem::path&, const ProjectManifest&,
    const TexturedPath&);

} // namespace fabric::project
