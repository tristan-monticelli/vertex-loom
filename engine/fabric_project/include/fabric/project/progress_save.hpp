#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_progress_save_schema_version = 1;

using ProgressValue = std::variant<bool, std::int64_t, double, std::string,
                                   core::Vec2, ResourceReference>;

struct ProgressSave {
    std::uint32_t schema_version{current_progress_save_schema_version};
    std::string build;
    ResourceReference scene{{}, "scene"};
    std::map<std::string, ProgressValue> properties;

    friend bool operator==(const ProgressSave&, const ProgressSave&) = default;
};

struct ProgressSaveResult {
    std::optional<ProgressSave> save;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return save.has_value() && errors.empty(); }
};

[[nodiscard]] ValidationReport validate_progress_save(const ProgressSave&);
[[nodiscard]] std::string serialize_progress_save(const ProgressSave&);
[[nodiscard]] ProgressSaveResult parse_progress_save(std::string_view);
[[nodiscard]] ProgressSaveResult load_progress_save(const std::filesystem::path&);
[[nodiscard]] ValidationReport save_progress_save_atomic(const std::filesystem::path&,
                                                         const ProgressSave&);

} // namespace fabric::project
