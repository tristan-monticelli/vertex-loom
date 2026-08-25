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

inline constexpr std::uint32_t current_texture_schema_version =
    current_asset_schema_version;
inline constexpr std::uint32_t current_raster_view_schema_version = 1;

enum class RasterFilter { nearest, linear };

[[nodiscard]] std::string_view to_string(RasterFilter filter) noexcept;

struct RasterView {
    std::uint32_t schema_version{current_raster_view_schema_version};
    core::Rect crop;
    core::Vec2 pivot{0.5F, 0.5F};
    core::Transform transform;
    RasterFilter filter{RasterFilter::linear};

    friend bool operator==(const RasterView&, const RasterView&) = default;
};

struct TextureAsset {
    AssetDocument document{
        .schema_version = current_texture_schema_version,
        .type = "texture",
    };
    std::filesystem::path source;
    std::uint32_t width{};
    std::uint32_t height{};
    std::string pixel_format{"rgba8"};
    // Absent means the complete source image, preserving historical documents.
    std::optional<RasterView> view;

    friend bool operator==(const TextureAsset&, const TextureAsset&) = default;
};

struct TextureAssetResult {
    std::optional<TextureAsset> asset;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path texture_source_path(
    const ProjectManifest& manifest, const core::ResourceId& id);
[[nodiscard]] std::filesystem::path texture_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id);
[[nodiscard]] ValidationReport validate_texture_asset(
    const ProjectManifest& manifest, const TextureAsset& asset);
[[nodiscard]] ValidationReport validate_raster_view(
    const RasterView& view, std::uint32_t source_width,
    std::uint32_t source_height);
[[nodiscard]] std::string serialize_texture_asset(const TextureAsset& asset);
[[nodiscard]] TextureAssetResult parse_texture_asset(
    const ProjectManifest& manifest, std::string_view json_text);
[[nodiscard]] TextureAssetResult load_texture_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path);
[[nodiscard]] TextureAssetResult publish_texture_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const TextureAsset& asset,
    const std::filesystem::path& validated_source);
[[nodiscard]] ValidationReport save_texture_asset_document(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, const TextureAsset& asset);

} // namespace fabric::project
