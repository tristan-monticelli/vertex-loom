#pragma once

#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <functional>
#include <string_view>

namespace fabric::project {

using DocumentValidator =
    std::function<ValidationReport(std::string_view serialized_document)>;

[[nodiscard]] ValidationReport save_document_atomic(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    std::string_view serialized_document,
    const DocumentValidator& validator);

} // namespace fabric::project
