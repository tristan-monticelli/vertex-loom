#include "fabric/runtime/preview_runtime.hpp"

#include "fabric/project/entity.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
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

fabric::project::ReplayDocument replay() {
    return {.document = {.schema_version = 1,
                         .type = "replay",
                         .id = {.value = "smoke-replay"},
                         .name = "Smoke Replay"},
            .build = "test-build",
            .seed = 42,
            .inputs = {{0, "jump", true, false}, {1, "jump", false, true}},
            .events = {{1, "opened", ""}},
            .checkpoints = {{1, {{"player", 0, 0, 0}}}}};
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
    result.instances.push_back({"offscreen",
                                fabric::project::ResourceReference{
                                    {.value = "runtime-entity"}, "entity"},
                                std::nullopt, "instances",
                                {.position = {10000.0F, 10000.0F}}, 156, 156, {}});
    return result;
}

fabric::project::EntityDefinition texture_entity() {
    return {.document = {.schema_version = 1,
                         .type = "entity",
                         .id = {.value = "runtime-texture-entity"},
                         .name = "Runtime Texture Entity"},
            .nodes = {{.id = "root",
                       .name = "Root",
                       .drawable = {.kind = fabric::project::EntityDrawableKind::texture,
                                     .resource = fabric::project::ResourceReference{
                                         {.value = "runtime-texture"}, "texture"}}}}};
}

fabric::project::MapDocument map_with_texture_entity() {
    auto result = map();
    result.instances.push_back({"textured",
                                fabric::project::ResourceReference{
                                    {.value = "runtime-texture-entity"}, "entity"},
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
    REQUIRE(runtime.stats().physics_steps == 1);
    REQUIRE(runtime.stats().p95_frame_ms >= 0.0);

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

TEST_CASE("preview runtime consumes a replay on fixed physics frames") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-replay-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map()).ok());
    REQUIRE(fabric::project::publish_replay(root, manifest(), replay()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .replay_id = fabric::core::ResourceId{.value = "smoke-replay"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test,
                          .frame_limit = 2}));
    REQUIRE(runtime.replay().has_value());
    REQUIRE(runtime.run());
    CHECK(runtime.stats().replay_events == 1);
    CHECK(runtime.stats().replay_checkpoints == 1);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime drives the character controller from replay actions") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-character-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map()).ok());
    auto movement = replay();
    movement.document.id.value = "movement-replay";
    movement.inputs = {{0, "move_right", true, false}};
    movement.events.clear();
    movement.checkpoints.clear();
    REQUIRE(fabric::project::publish_replay(root, project_manifest, movement).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .replay_id = fabric::core::ResourceId{.value = "movement-replay"},
                          .enable_character = true,
                          .mode = fabric::runtime::RuntimeMode::smoke_test,
                          .frame_limit = 2}));
    REQUIRE(runtime.run());
    CHECK(runtime.stats().character_x > 0.0F);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
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

TEST_CASE("preview runtime uploads texture entity drawables") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-texture-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    const auto input = root / "input.png";
    constexpr std::array<unsigned char, 70> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9c, 0x63, 0x60, 0xf8, 0xcf, 0xc0,
        0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xdd,
        0x8d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::ofstream output(input, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
    output.close();
    REQUIRE(fabric::project::publish_texture_asset(
        root, manifest(),
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "runtime-texture"},
                      .name = "Runtime Texture"},
         .source = "assets/textures/runtime-texture.png",
         .width = 1,
         .height = 1}, input).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), texture_entity()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_texture_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.stats().visible_instances == 1);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace
