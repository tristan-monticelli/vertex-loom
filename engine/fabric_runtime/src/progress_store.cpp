#include "fabric/runtime/progress_store.hpp"

#include <SDL.h>

#include <algorithm>

namespace fabric::runtime {
namespace {

void append_errors(std::vector<std::string>& output,
                   const std::vector<project::Error>& errors) {
    for (const auto& error : errors)
        output.push_back(error.field + ": " + error.message);
}

} // namespace

bool ProgressStore::configure_user_path(const std::string_view organization,
                                        const std::string_view application,
                                        const std::string_view slot) {
    errors_.clear();
    if (organization.empty() || application.empty() || slot.empty() ||
        slot.find('/') != std::string_view::npos || slot.find('\\') != std::string_view::npos) {
        errors_.push_back("organization, application and a safe slot are required");
        return false;
    }
    char* preference_path = SDL_GetPrefPath(std::string(organization).c_str(),
                                            std::string(application).c_str());
    if (preference_path == nullptr) {
        errors_.push_back(SDL_GetError());
        return false;
    }
    path_ = std::filesystem::path(preference_path) / std::string(slot);
    SDL_free(preference_path);
    return true;
}

bool ProgressStore::configure_path(std::filesystem::path path) {
    errors_.clear();
    const auto has_traversal = std::ranges::any_of(path, [](const auto& component) {
        return component == "..";
    });
    if (path.empty() || has_traversal || path.filename().empty() ||
        path.filename() == "." || path.filename() == "..") {
        errors_.push_back("progress path must be a filename inside its configured directory");
        return false;
    }
    path_ = std::move(path);
    return true;
}

bool ProgressStore::load(project::ProgressSave& destination) const {
    errors_.clear();
    if (path_.empty()) {
        errors_.push_back("progress store is not configured");
        return false;
    }
    const auto loaded = project::load_progress_save(path_);
    if (!loaded.ok()) {
        append_errors(errors_, loaded.errors);
        return false;
    }
    destination = *loaded.save;
    return true;
}

bool ProgressStore::save(const project::ProgressSave& source) const {
    errors_.clear();
    if (path_.empty()) {
        errors_.push_back("progress store is not configured");
        return false;
    }
    const auto result = project::save_progress_save_atomic(path_, source);
    if (!result.ok()) {
        append_errors(errors_, result.errors);
        return false;
    }
    return true;
}

} // namespace fabric::runtime
