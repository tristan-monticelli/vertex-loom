#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/asset.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_vector_schema_version = 2;

enum class VectorSourceKind {
    linked_svg,
    native,
};

[[nodiscard]] std::string_view to_string(VectorSourceKind kind) noexcept;

enum class VectorOrigin {
    center,
    top_left,
};

enum class VectorShapeKind {
    rectangle,
    ellipse,
    line,
    path,
};

enum class VectorPathCommandKind {
    move,
    line,
    cubic,
    close,
};

enum class VectorFillKind {
    solid,
    image,
    none,
};

enum class VectorStrokeJoin {
    miter,
    round,
    bevel,
};

enum class VectorStrokeCap {
    butt,
    round,
    square,
};

enum class VectorImageFit {
    contain,
    cover,
    stretch,
    free,
};

[[nodiscard]] std::string_view to_string(VectorOrigin origin) noexcept;
[[nodiscard]] std::string_view to_string(VectorShapeKind kind) noexcept;
[[nodiscard]] std::string_view to_string(VectorPathCommandKind kind) noexcept;
[[nodiscard]] std::string_view to_string(VectorFillKind kind) noexcept;
[[nodiscard]] std::string_view to_string(VectorStrokeJoin join) noexcept;
[[nodiscard]] std::string_view to_string(VectorStrokeCap cap) noexcept;
[[nodiscard]] std::string_view to_string(VectorImageFit fit) noexcept;

struct VectorImageFill {
    ResourceReference texture{{}, "texture"};
    VectorImageFit fit{VectorImageFit::cover};
    core::Transform transform;
    float opacity{1.0F};
    bool deform_with_shape{true};

    friend bool operator==(const VectorImageFill&,
                           const VectorImageFill&) = default;
};

struct VectorFill {
    VectorFillKind kind{VectorFillKind::none};
    std::optional<core::Color> color;
    std::optional<VectorImageFill> image;

    friend bool operator==(const VectorFill&, const VectorFill&) = default;
};

struct VectorStroke {
    core::Color color{0.9F, 0.9F, 0.9F, 1.0F};
    float width{1.0F};
    VectorStrokeJoin join{VectorStrokeJoin::miter};
    VectorStrokeCap cap{VectorStrokeCap::butt};

    friend bool operator==(const VectorStroke&, const VectorStroke&) = default;
};

struct VectorShape {
    std::string id;
    VectorShapeKind kind{VectorShapeKind::rectangle};
    core::Rect bounds;
    std::vector<core::Vec2> points;

    struct PathCommand {
        VectorPathCommandKind kind{VectorPathCommandKind::move};
        core::Vec2 point;
        core::Vec2 control1;
        core::Vec2 control2;

        friend bool operator==(const PathCommand&, const PathCommand&) = default;
    };
    std::vector<PathCommand> path;

    friend bool operator==(const VectorShape&, const VectorShape&) = default;
};

struct VectorNode {
    std::string id;
    std::string name;
    bool visible{true};
    bool locked{};
    core::Transform transform;
    VectorShape shape;
    VectorFill fill;
    std::optional<VectorStroke> stroke;

    friend bool operator==(const VectorNode&, const VectorNode&) = default;
};

struct NativeVectorDefinition {
    core::Vec2 size;
    VectorOrigin origin{VectorOrigin::center};
    std::vector<VectorNode> nodes;

    friend bool operator==(const NativeVectorDefinition&,
                           const NativeVectorDefinition&) = default;
};

struct VectorAsset {
    AssetDocument document{
        .schema_version = current_vector_schema_version,
        .type = "vector",
    };
    VectorSourceKind source_kind{VectorSourceKind::linked_svg};
    std::filesystem::path source;
    std::optional<NativeVectorDefinition> native;

    friend bool operator==(const VectorAsset&, const VectorAsset&) = default;
};

struct VectorAssetResult {
    std::optional<VectorAsset> asset;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path vector_source_path(
    const ProjectManifest& manifest, const core::ResourceId& id);
[[nodiscard]] std::filesystem::path vector_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id);
[[nodiscard]] ValidationReport validate_vector_asset(
    const ProjectManifest& manifest, const VectorAsset& asset);
[[nodiscard]] std::vector<ResourceReference> vector_resource_references(
    const VectorAsset& asset);
[[nodiscard]] std::string serialize_vector_asset(const VectorAsset& asset);
[[nodiscard]] VectorAssetResult parse_vector_asset(
    const ProjectManifest& manifest, std::string_view json_text);
[[nodiscard]] VectorAssetResult load_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path);
[[nodiscard]] VectorAssetResult publish_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const VectorAsset& asset,
    const std::filesystem::path& validated_source);
[[nodiscard]] VectorAssetResult publish_native_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const VectorAsset& asset);

} // namespace fabric::project
