#pragma once

#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <string_view>

namespace fabric::project::detail {

[[nodiscard]] bool is_portable_relative_path(
    const std::filesystem::path& path);
[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& candidate);
[[nodiscard]] ValidationReport publish_asset_files(
    const std::filesystem::path& project_root,
    const std::filesystem::path& asset_directory,
    const std::filesystem::path& source_relative,
    const std::filesystem::path& document_relative,
    const std::filesystem::path& validated_source,
    std::string_view serialized_document,
    std::string_view asset_kind);

} // namespace fabric::project::detail
