#pragma once

#include "fabric/project/asset.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_sprite_sheet_schema_version = 1;

enum class SpriteSourceKind {
    aseprite,
    png,
};

struct SpriteRect {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};

    friend bool operator==(const SpriteRect&, const SpriteRect&) = default;
};

struct SpriteSize {
    std::uint32_t width{};
    std::uint32_t height{};

    friend bool operator==(const SpriteSize&, const SpriteSize&) = default;
};

struct SpritePoint {
    std::int32_t x{};
    std::int32_t y{};

    friend bool operator==(const SpritePoint&, const SpritePoint&) = default;
};

struct SpriteSliceRect {
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};

    friend bool operator==(const SpriteSliceRect&,
                           const SpriteSliceRect&) = default;
};

struct SpriteFrameDefinition {
    std::string name;
    SpriteRect atlas_bounds;
    SpriteRect source_bounds;
    SpriteSize source_size;
    std::uint32_t duration_ms{};
    std::optional<SpritePoint> pivot;

    friend bool operator==(const SpriteFrameDefinition&,
                           const SpriteFrameDefinition&) = default;
};

struct SpriteTagDefinition {
    std::string name;
    std::uint32_t from_frame{};
    std::uint32_t to_frame{};
    std::string direction{"forward"};
    std::uint32_t repeat{};

    friend bool operator==(const SpriteTagDefinition&,
                           const SpriteTagDefinition&) = default;
};

struct SpriteSliceKeyDefinition {
    std::uint32_t frame{};
    SpriteSliceRect bounds;
    std::optional<SpriteSliceRect> center;
    std::optional<SpritePoint> pivot;

    friend bool operator==(const SpriteSliceKeyDefinition&,
                           const SpriteSliceKeyDefinition&) = default;
};

struct SpriteSliceDefinition {
    std::string name;
    std::vector<SpriteSliceKeyDefinition> keys;

    friend bool operator==(const SpriteSliceDefinition&,
                           const SpriteSliceDefinition&) = default;
};

struct SpriteSheetDefinition {
    AssetDocument document{
        .schema_version = current_sprite_sheet_schema_version,
        .type = "spriteSheet",
    };
    SpriteSourceKind source_kind{SpriteSourceKind::aseprite};
    std::filesystem::path source;
    std::filesystem::path atlas;
    SpriteSize atlas_size;
    std::uint32_t padding{1};
    std::uint32_t extrusion{1};
    std::vector<SpriteFrameDefinition> frames;
    std::vector<SpriteTagDefinition> tags;
    std::vector<SpriteSliceDefinition> slices;

    friend bool operator==(const SpriteSheetDefinition&,
                           const SpriteSheetDefinition&) = default;
};

struct SpriteSheetResult {
    std::optional<SpriteSheetDefinition> asset;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path sprite_sheet_source_path(
    const ProjectManifest& manifest, const core::ResourceId& id,
    SpriteSourceKind kind);
[[nodiscard]] std::filesystem::path sprite_sheet_atlas_path(
    const ProjectManifest& manifest, const core::ResourceId& id);
[[nodiscard]] std::filesystem::path sprite_sheet_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id);
[[nodiscard]] ValidationReport validate_sprite_sheet(
    const ProjectManifest& manifest, const SpriteSheetDefinition& definition);
[[nodiscard]] std::string serialize_sprite_sheet(
    const SpriteSheetDefinition& definition);
[[nodiscard]] SpriteSheetResult parse_sprite_sheet(
    const ProjectManifest& manifest, std::string_view json_text);
[[nodiscard]] SpriteSheetResult load_sprite_sheet(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path);
[[nodiscard]] SpriteSheetResult publish_sprite_sheet(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const SpriteSheetDefinition& definition,
    const std::filesystem::path& validated_source,
    std::span<const std::uint8_t> atlas_png);

} // namespace fabric::project
