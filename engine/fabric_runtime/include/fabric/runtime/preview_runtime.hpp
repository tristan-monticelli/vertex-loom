#pragma once

#include "fabric/core/resource_id.hpp"
#include "fabric/physics/physics_world.hpp"
#include "fabric/project/map.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabric::runtime {

enum class RuntimeMode { interactive, smoke_test, benchmark };

struct PreviewRuntimeOptions {
    std::filesystem::path project_root;
    core::ResourceId map_id;
    RuntimeMode mode{RuntimeMode::interactive};
    std::int32_t width{1440};
    std::int32_t height{900};
    std::size_t frame_limit{};
};

struct PreviewRuntimeStats {
    std::size_t frames{};
    std::size_t physics_steps{};
    std::size_t visible_instances{};
    std::size_t draw_calls{};
    std::size_t triangles{};
    double elapsed_ms{};
    double p95_frame_ms{};
};

class PreviewRuntime {
public:
    PreviewRuntime();
    ~PreviewRuntime();
    PreviewRuntime(const PreviewRuntime&) = delete;
    PreviewRuntime& operator=(const PreviewRuntime&) = delete;

    // Performs every project and map validation before any SDL window exists.
    [[nodiscard]] bool load(const PreviewRuntimeOptions& options);
    [[nodiscard]] bool run();

    [[nodiscard]] bool loaded() const noexcept { return map_.has_value(); }
    [[nodiscard]] const std::optional<project::MapDocument>& map() const noexcept {
        return map_;
    }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept {
        return errors_;
    }
    [[nodiscard]] const PreviewRuntimeStats& stats() const noexcept { return stats_; }

private:
    struct Impl;

    PreviewRuntimeOptions options_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::MapDocument> map_;
    physics::PhysicsWorld physics_;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> errors_;
    PreviewRuntimeStats stats_;
};

} // namespace fabric::runtime
