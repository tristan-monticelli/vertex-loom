#pragma once

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

struct VectorAsset {
    AssetDocument document{
        .schema_version = current_vector_schema_version,
        .type = "vector",
    };
    VectorSourceKind source_kind{VectorSourceKind::linked_svg};
    std::filesystem::path source;

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

} // namespace fabric::project
