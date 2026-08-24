#pragma once

#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_texture_schema_version = 1;

struct AssetDocument {
    std::uint32_t schema_version{current_texture_schema_version};
    std::string type{"texture"};
    core::ResourceId id;
    std::string name;

    friend bool operator==(const AssetDocument&, const AssetDocument&) = default;
};

struct TextureAsset {
    AssetDocument document;
    std::filesystem::path source;
    std::uint32_t width{};
    std::uint32_t height{};
    std::string pixel_format{"rgba8"};

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

} // namespace fabric::project
