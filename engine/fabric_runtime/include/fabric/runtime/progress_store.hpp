#pragma once

#include "fabric/project/progress_save.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::runtime {

class ProgressStore {
public:
    [[nodiscard]] bool configure_user_path(std::string_view organization,
                                            std::string_view application,
                                            std::string_view slot = "progress.json");
    [[nodiscard]] bool configure_path(std::filesystem::path path);

    [[nodiscard]] bool load(project::ProgressSave& destination) const;
    [[nodiscard]] bool save(const project::ProgressSave& source) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }

private:
    std::filesystem::path path_;
    mutable std::vector<std::string> errors_;
};

} // namespace fabric::runtime
