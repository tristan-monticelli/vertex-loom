#include "fabric/runtime/preview_runtime.hpp"

#include "fabric/project/entity.hpp"
#include "fabric/project/vector_asset.hpp"

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

fabric::project::VectorAsset vector_asset() {
    return {.document = {.schema_version = 2,
                         .type = "vector",
                         .id = {.value = "runtime-vector"},
                         .name = "Runtime Vector"},
            .source_kind = fabric::project::VectorSourceKind::native,
            .native = fabric::project::NativeVectorDefinition{
                .size = {2.0F, 2.0F},
                .nodes = {{.id = "shape",
                           .name = "Shape",
                           .shape = {.id = "shape",
                                     .bounds = {{-1.0F, -1.0F}, {2.0F, 2.0F}}},
                           .fill = {.kind = fabric::project::VectorFillKind::solid,
                                    .color = fabric::core::Color{1.0F, 0.2F, 0.1F, 1.0F}}}}}};
}

fabric::project::EntityDefinition entity() {
    return {.document = {.schema_version = 1,
                         .type = "entity",
                         .id = {.value = "runtime-entity"},
                         .name = "Runtime Entity"},
            .nodes = {{.id = "root",
                       .name = "Root",
                       .drawable = {.kind = fabric::project::EntityDrawableKind::vector,
                                     .resource = fabric::project::ResourceReference{
                                         {.value = "runtime-vector"}, "vector"}}}}};
}

fabric::project::MapDocument map_with_entity() {
    auto result = map();
    result.instances.push_back({"marker",
                                fabric::project::ResourceReference{
                                    {.value = "runtime-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    return result;
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

TEST_CASE("preview runtime resolves native vector entity drawables") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-drawables-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.stats().visible_instances == 1);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace
