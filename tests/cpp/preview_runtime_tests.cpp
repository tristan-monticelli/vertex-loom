#include "fabric/runtime/preview_runtime.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "runtime-test"},
            .name = "Runtime Test"};
}

fabric::project::MapDocument map() {
    return {.document = {.schema_version = 1,
                         .type = "map",
                         .id = {.value = "preview"},
                         .name = "Preview"},
            .layers = {{"instances", "Instances",
                        fabric::project::MapLayerKind::instances, true, false, 0.0F}}};
}

TEST_CASE("preview runtime validates and loads a map before graphics") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-runtime-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map()).ok());

    fabric::runtime::PreviewRuntime runtime;
    const auto loaded = runtime.load({.project_root = root, .map_id = {.value = "preview"},
                                      .mode = fabric::runtime::RuntimeMode::smoke_test});
    if (!loaded) for (const auto& error : runtime.errors()) std::cerr << error << '\n';
    REQUIRE(loaded);
    REQUIRE(runtime.loaded());
    REQUIRE(runtime.map()->instances.empty());
    REQUIRE(runtime.errors().empty());
    const auto ran = runtime.run();
    if (!ran) for (const auto& error : runtime.errors()) std::cerr << error << '\n';
    REQUIRE(ran);
    REQUIRE(runtime.stats().frames == 1);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime rejects invalid projects before loading") {
    fabric::runtime::PreviewRuntime runtime;
    REQUIRE_FALSE(runtime.load({.project_root = "/definitely/missing/project",
                                 .map_id = {.value = "preview"}}));
    REQUIRE_FALSE(runtime.loaded());
    REQUIRE_FALSE(runtime.errors().empty());
}

} // namespace
