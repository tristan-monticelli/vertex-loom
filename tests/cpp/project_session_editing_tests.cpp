#include "fabric/editor/project_session.hpp"
#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/visual_presets.hpp"
#include "fabric/project/document_storage.hpp"
#include "fabric/project/map.hpp"
#include "fabric/project/mechanic_graph.hpp"
#include "fabric/project/replay.hpp"
#include "fabric/project/scene.hpp"
#include "fabric/render/textured_path_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("fabric-session-editing-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_project(const std::filesystem::path& root) {
    for (const auto* directory :
         {"assets", "entities", "maps", "scenes", "schemas"}) {
        std::filesystem::create_directory(root / directory);
    }
    const fabric::project::ProjectManifest manifest{
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "editing-project"},
        .name = "Primary",
        .directories = {},
    };
    std::ofstream output(root / "project.json", std::ios::binary);
    output << fabric::project::serialize_manifest(manifest);
}

fabric::project::ProjectManifest load_manifest_or_fail(
    const std::filesystem::path& root) {
    auto loaded = fabric::project::load_manifest(root);
    REQUIRE(loaded.ok());
    return std::move(*loaded.manifest);
}

} // namespace

TEST_CASE("project manifest edits use command history and explicit save") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};

    REQUIRE(session.set_pixels_per_unit(64.0, start));
    CHECK(session.manifest()->pixels_per_unit == 64.0);
    CHECK(session.dirty());
    CHECK(session.can_undo());
    CHECK(load_manifest_or_fail(project.path()).pixels_per_unit == 100.0);
    CHECK_FALSE(session.set_pixels_per_unit(
        std::numeric_limits<double>::quiet_NaN(), start));
    CHECK(session.manifest()->pixels_per_unit == 64.0);

    REQUIRE(session.undo(start + std::chrono::milliseconds{1}));
    CHECK(session.manifest()->pixels_per_unit == 100.0);
    CHECK_FALSE(session.dirty());
    REQUIRE(session.redo(start + std::chrono::milliseconds{2}));
    CHECK(session.manifest()->pixels_per_unit == 64.0);
    CHECK(session.dirty());

    REQUIRE(session.save());
    CHECK_FALSE(session.dirty());
    CHECK(load_manifest_or_fail(project.path()).pixels_per_unit == 64.0);
}

TEST_CASE("resource index includes maps scenes mechanics and replays") {
    const TemporaryDirectory project;
    write_project(project.path());
    const auto manifest = load_manifest_or_fail(project.path());
    REQUIRE(fabric::project::publish_map(project.path(), manifest, {
        .document = {.schema_version = fabric::project::current_map_schema_version,
                     .type = "map", .id = {.value = "indexed-map"},
                     .name = "Indexed Map"}}).ok());
    REQUIRE(fabric::project::publish_scene(project.path(), manifest, {
        .document = {.schema_version = fabric::project::current_scene_schema_version,
                     .type = "scene", .id = {.value = "indexed-scene"},
                     .name = "Indexed Scene"},
        .maps = {{{{.value = "indexed-map"}, "map"}, "world"}},
        .entry_map = fabric::project::ResourceReference{
            {.value = "indexed-map"}, "map"}}).ok());
    REQUIRE(fabric::project::publish_mechanic_graph(project.path(), manifest, {
        .document = {
            .schema_version = fabric::project::current_mechanic_graph_schema_version,
            .type = "mechanic", .id = {.value = "indexed-mechanic"},
            .name = "Indexed Mechanic"}}).ok());
    REQUIRE(fabric::project::publish_replay(project.path(), manifest, {
        .document = {.schema_version = fabric::project::current_replay_schema_version,
                     .type = "replay", .id = {.value = "indexed-replay"},
                     .name = "Indexed Replay"},
        .build = "test-build"}).ok());

    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    for (const auto kind : {
             fabric::editor::StudioResourceKind::map,
             fabric::editor::StudioResourceKind::scene,
             fabric::editor::StudioResourceKind::mechanic,
             fabric::editor::StudioResourceKind::replay}) {
        const auto found = std::ranges::find(session.resources(), kind,
                                             &fabric::editor::StudioResource::kind);
        REQUIRE(found != session.resources().end());
        REQUIRE(session.select_resource(found->kind, found->id));
        CHECK(session.selected_resource()->document_path == found->document_path);
        const auto source_id = found->id;
        const fabric::core::ResourceId copy_id{
            .value = source_id.value + "-copy"};
        REQUIRE(session.duplicate_resource(kind, source_id, copy_id,
                                           "Indexed Copy"));
        REQUIRE(session.selected_resource() != nullptr);
        CHECK(session.selected_resource()->id == copy_id);
        REQUIRE(session.rename_resource(kind, copy_id, "Renamed Copy"));
        CHECK(session.selected_resource()->name == "Renamed Copy");
        CHECK_FALSE(session.rename_resource(kind, copy_id, ""));
        CHECK_FALSE(session.duplicate_resource(kind, source_id, copy_id,
                                               "Collision"));
    }

    const auto incoming = session.incoming_references(
        fabric::editor::StudioResourceKind::map, {.value = "indexed-map"});
    REQUIRE(incoming.has_value());
    CHECK(incoming->size() == 2U);
    CHECK_FALSE(session.trash_resource(
        fabric::editor::StudioResourceKind::map, {.value = "indexed-map"},
        true));
    CHECK_FALSE(session.trash_resource(
        fabric::editor::StudioResourceKind::replay,
        {.value = "indexed-replay"}, false));
    REQUIRE(session.trash_resource(
        fabric::editor::StudioResourceKind::replay,
        {.value = "indexed-replay"}, true));
    CHECK(session.can_restore_trashed_resource());
    CHECK_FALSE(std::filesystem::exists(
        project.path() / "assets/replays/indexed-replay.replay.json"));
    REQUIRE(session.restore_trashed_resource());
    CHECK(std::filesystem::is_regular_file(
        project.path() / "assets/replays/indexed-replay.replay.json"));
    CHECK_FALSE(session.can_restore_trashed_resource());
}

TEST_CASE("vector artwork prompt publishes a reloadable native document") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    fabric::editor::CreateVectorArtworkPrompt prompt;
    prompt.name = "Centered ellipse";
    prompt.width = 12.0;
    prompt.height = 8.0;
    prompt.first_shape = fabric::editor::InitialShape::ellipse;
    prompt.initial_color = {0.25F, 0.5F, 0.75F, 1.0F};

    REQUIRE(session.create_vector_artwork(prompt));
    REQUIRE(session.created_vector().has_value());
    CHECK(session.created_vector()->source_kind ==
          fabric::project::VectorSourceKind::native);
    REQUIRE(session.created_vector()->native.has_value());
    REQUIRE(session.created_vector()->native->nodes.size() == 1);
    CHECK(session.created_vector()->native->nodes.front().shape.kind ==
          fabric::project::VectorShapeKind::ellipse);
    CHECK(session.created_vector()->native->nodes.front().shape.bounds.origin ==
          fabric::core::Vec2{-6.0F, -4.0F});

    auto loaded = fabric::project::load_vector_asset(
        project.path(), *session.manifest(),
        "assets/vectors/centered-ellipse.vector.json");
    REQUIRE(loaded.ok());
    CHECK(*loaded.asset == *session.created_vector());
    CHECK_FALSE(std::filesystem::exists(
        project.path() / "assets/vectors/centered-ellipse.svg"));

    prompt.name = "Transparent panel";
    prompt.origin = fabric::editor::ArtworkOrigin::top_left;
    prompt.first_shape = fabric::editor::InitialShape::rectangle;
    prompt.initial_fill = fabric::editor::InitialFill::transparent;
    REQUIRE(session.create_vector_artwork(prompt));
    REQUIRE(session.created_vector()->native.has_value());
    const auto& transparent = session.created_vector()->native->nodes.front();
    CHECK(transparent.shape.bounds.origin == fabric::core::Vec2{});
    CHECK(transparent.fill.kind == fabric::project::VectorFillKind::none);
    CHECK_FALSE(transparent.fill.color.has_value());

    const auto image_source = project.path() / "image-fill.png";
    std::ofstream{image_source, std::ios::binary} << "source";
    const fabric::project::TextureAsset texture{
        .document = {
            .type = "texture",
            .id = {.value = "image-fill"},
            .name = "Image Fill",
        },
        .source = "assets/textures/image-fill.png",
        .width = 1,
        .height = 1,
    };
    REQUIRE(fabric::project::publish_texture_asset(
                project.path(), *session.manifest(), texture, image_source)
                .ok());
    prompt.name = "Image panel";
    prompt.initial_fill = fabric::editor::InitialFill::image;
    prompt.initial_image_id = "image-fill";
    prompt.image_fit = fabric::project::VectorImageFit::free;
    prompt.image_transform.position = {0.2F, 0.3F};
    prompt.image_transform.scale = {1.4F, 0.7F};
    prompt.deform_image_with_shape = true;
    REQUIRE(session.create_vector_artwork(prompt));
    const auto& image =
        *session.created_vector()->native->nodes.front().fill.image;
    CHECK(image.texture.id.value == "image-fill");
    CHECK(image.fit == fabric::project::VectorImageFit::free);
    CHECK(image.transform.position == fabric::core::Vec2{0.2F, 0.3F});
    CHECK(image.deform_with_shape);
}

TEST_CASE("resource creation and selection save the previous dirty document") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));

    fabric::editor::CreateVectorArtworkPrompt artwork;
    artwork.name = "Transition artwork";
    REQUIRE(session.create_vector_artwork(artwork));
    auto node = session.created_vector()->native->nodes.front();
    node.name = "Saved before creation";
    REQUIRE(session.set_selected_vector_node(0U, node));
    REQUIRE(session.dirty());

    fabric::editor::CreateMaterialPrompt material;
    material.name = "Transition material";
    REQUIRE(session.create_material(material));
    CHECK_FALSE(session.dirty());
    REQUIRE(session.selected_material().has_value());

    auto persisted = fabric::project::load_vector_asset(
        project.path(), *session.manifest(),
        "assets/vectors/transition-artwork.vector.json");
    REQUIRE(persisted.ok());
    CHECK(persisted.asset->native->nodes.front().name ==
          "Saved before creation");

    REQUIRE(session.select_resource(
        fabric::editor::StudioResourceKind::vector,
        {.value = "transition-artwork"}));
    node = session.created_vector()->native->nodes.front();
    node.name = "Saved before selection";
    REQUIRE(session.set_selected_vector_node(0U, node));
    REQUIRE(session.select_resource(
        fabric::editor::StudioResourceKind::material,
        {.value = "transition-material"}));
    CHECK_FALSE(session.dirty());

    persisted = fabric::project::load_vector_asset(
        project.path(), *session.manifest(),
        "assets/vectors/transition-artwork.vector.json");
    REQUIRE(persisted.ok());
    CHECK(persisted.asset->native->nodes.front().name ==
          "Saved before selection");
}

TEST_CASE("project session creates indexes and reloads a visual preset") {
    const TemporaryDirectory project;
    write_project(project.path());
    const auto manifest = load_manifest_or_fail(project.path());
    const auto thread_source = project.path() / "thread.png";
    std::ofstream{thread_source, std::ios::binary} << "thread-source";
    REQUIRE(fabric::project::publish_texture_asset(
        project.path(), manifest,
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "cotton-thread"},
                      .name = "Cotton Thread"},
         .source = "assets/textures/cotton-thread.png",
         .width = 8U,
         .height = 8U},
        thread_source).ok());

    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    const fabric::editor::VisualPresetRequest request{
        .kind = fabric::editor::VisualPresetKind::zipper,
        .id = {.value = "coat-zipper"},
        .name = "Coat zipper",
        .thread_texture = fabric::project::ResourceReference{
            {.value = "cotton-thread"}, "texture"},
        .zipper_tooth_count = 8U};
    REQUIRE(session.create_visual_preset(request));
    REQUIRE(session.selected_resource() != nullptr);
    CHECK(session.selected_resource()->kind ==
          fabric::editor::StudioResourceKind::visual_component);
    REQUIRE(session.selected_visual_component().has_value());
    CHECK(session.selected_visual_component()->document.id.value ==
          "coat-zipper");
    CHECK(std::ranges::count_if(
              session.resources(), [](const auto& resource) {
                  return resource.kind ==
                      fabric::editor::StudioResourceKind::textured_path;
              }) == 2);

    fabric::editor::CreateEntityPrompt entity_prompt;
    entity_prompt.name = "Zipper entity";
    entity_prompt.drawable =
        fabric::project::EntityDrawableKind::visual_component;
    entity_prompt.resource_id = "coat-zipper";
    REQUIRE(session.create_entity(entity_prompt));
    REQUIRE(session.selected_entity().has_value());
    const auto& drawable = session.selected_entity()->nodes.front().drawable;
    CHECK(drawable.kind ==
          fabric::project::EntityDrawableKind::visual_component);
    REQUIRE(drawable.component_instance.has_value());
    CHECK(drawable.resource->expected_type == "visualComponent");

    fabric::editor::ProjectSession reopened;
    REQUIRE(reopened.open(project.path()));
    REQUIRE(reopened.select_resource(
        fabric::editor::StudioResourceKind::visual_component,
        {.value = "coat-zipper"}));
    REQUIRE(reopened.selected_visual_component().has_value());
    CHECK(reopened.selected_visual_component()->composition.id.value ==
          "coat-zipper-composition");
}

TEST_CASE("visual composition and component edits undo autosave and recover") {
    const TemporaryDirectory project;
    write_project(project.path());
    const auto manifest = load_manifest_or_fail(project.path());
    const auto thread_source = project.path() / "thread.png";
    std::ofstream{thread_source, std::ios::binary} << "thread-source";
    REQUIRE(fabric::project::publish_texture_asset(
        project.path(), manifest,
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "cotton-thread"},
                      .name = "Cotton Thread"},
         .source = "assets/textures/cotton-thread.png",
         .width = 8U,
         .height = 8U},
        thread_source).ok());

    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    REQUIRE(session.create_visual_preset({
        .kind = fabric::editor::VisualPresetKind::eye,
        .id = {.value = "editable-eye"},
        .name = "Editable eye",
        .thread_texture = fabric::project::ResourceReference{
            {.value = "cotton-thread"}, "texture"}}));
    REQUIRE(session.select_resource(
        fabric::editor::StudioResourceKind::visual_composition,
        {.value = "editable-eye-composition"}));

    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    auto composition = *session.selected_visual_composition();
    composition.layers.front().transform.position = {2.0F, -1.0F};
    composition.layers.front().anchor = {0.25F, 0.75F};
    composition.layers.front().z_order = 3.0F;
    composition.layers.front().visible = false;
    auto duplicate = composition.layers.front();
    duplicate.id = "eye-copy";
    duplicate.name = "Eye copy";
    duplicate.visible = true;
    composition.layers.push_back(duplicate);
    REQUIRE(session.set_selected_visual_composition(composition, start));
    REQUIRE(session.undo(start));
    CHECK(session.selected_visual_composition()->layers.size() == 1U);
    REQUIRE(session.redo(start));
    CHECK(session.selected_visual_composition()->layers.size() == 2U);
    CHECK(session.update_autosave(start + std::chrono::seconds{2}) ==
          fabric::editor::AutosaveStatus::saved);

    fabric::editor::ProjectSession recovered_composition;
    REQUIRE(recovered_composition.open(project.path()));
    REQUIRE(recovered_composition.select_resource(
        fabric::editor::StudioResourceKind::visual_composition,
        {.value = "editable-eye-composition"}));
    REQUIRE(recovered_composition.has_recovery());
    REQUIRE(recovered_composition.accept_recovery(
        start + std::chrono::seconds{3}));
    CHECK(recovered_composition.selected_visual_composition()->layers.size() ==
          2U);
    REQUIRE(recovered_composition.save());

    REQUIRE(session.undo(start + std::chrono::seconds{3}));
    REQUIRE(session.select_resource(
        fabric::editor::StudioResourceKind::visual_component,
        {.value = "editable-eye"}));
    auto component = *session.selected_visual_component();
    component.anchors.front().position = {0.5F, -0.25F};
    component.parameters.front().name = "Eye scale";
    component.parameters.front().default_value =
        fabric::core::Vec2{1.25F, 0.8F};
    REQUIRE(session.set_selected_visual_component(
        component, start + std::chrono::seconds{4}));
    REQUIRE(session.undo(start + std::chrono::seconds{4}));
    CHECK(session.selected_visual_component()->anchors.front().position ==
          fabric::core::Vec2{});
    REQUIRE(session.redo(start + std::chrono::seconds{4}));
    CHECK(session.update_autosave(start + std::chrono::seconds{6}) ==
          fabric::editor::AutosaveStatus::saved);

    fabric::editor::ProjectSession recovered_component;
    REQUIRE(recovered_component.open(project.path()));
    REQUIRE(recovered_component.select_resource(
        fabric::editor::StudioResourceKind::visual_component,
        {.value = "editable-eye"}));
    REQUIRE(recovered_component.has_recovery());
    REQUIRE(recovered_component.accept_recovery(
        start + std::chrono::seconds{7}));
    CHECK(recovered_component.selected_visual_component()
              ->anchors.front().position ==
          fabric::core::Vec2{0.5F, -0.25F});
    REQUIRE(recovered_component.save());

    fabric::editor::ProjectSession reloaded;
    REQUIRE(reloaded.open(project.path()));
    REQUIRE(reloaded.select_resource(
        fabric::editor::StudioResourceKind::visual_component,
        {.value = "editable-eye"}));
    CHECK(reloaded.selected_visual_component()->parameters.front().name ==
          "Eye scale");
}

TEST_CASE("textured path edits undo autosave recover and reload") {
    const TemporaryDirectory project;
    write_project(project.path());
    const auto manifest = load_manifest_or_fail(project.path());
    const auto thread_source = project.path() / "thread.png";
    std::ofstream{thread_source, std::ios::binary} << "thread-source";
    REQUIRE(fabric::project::publish_texture_asset(
        project.path(), manifest,
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "cotton-thread"},
                      .name = "Cotton Thread"},
         .source = "assets/textures/cotton-thread.png",
         .width = 8U,
         .height = 8U},
        thread_source).ok());

    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    REQUIRE(session.create_visual_preset({
        .kind = fabric::editor::VisualPresetKind::seam,
        .id = {.value = "editable-seam"},
        .name = "Editable seam",
        .thread_texture = fabric::project::ResourceReference{
            {.value = "cotton-thread"}, "texture"}}));
    REQUIRE(session.select_resource(
        fabric::editor::StudioResourceKind::textured_path,
        {.value = "editable-seam-rail"}));

    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    auto path = *session.selected_textured_path();
    path.commands.front().point = {-3.0F, -1.0F};
    path.commands.back().control1 = {-1.0F, 1.0F};
    path.commands.back().control2 = {1.0F, -1.0F};
    path.commands.push_back({
        .kind = fabric::project::TexturedPathCommandKind::line,
        .point = {3.0F, 1.0F}});
    path.width = 0.25F;
    path.uv_scale = {8.0F, 1.5F};
    path.uv_offset = {0.2F, 0.1F};
    path.color = {0.8F, 0.4F, 0.2F, 1.0F};
    path.opacity = 0.75F;
    REQUIRE(session.set_selected_textured_path(path, start));
    const auto preview = fabric::render::build_textured_path_draw_packets(
        *session.selected_textured_path());
    REQUIRE(preview.ok());
    REQUIRE(preview.packets.size() == 1U);
    CHECK(preview.packets.front().fill_uv.front().x == 0.2F);
    CHECK(preview.packets.front().fill_color == path.color);
    REQUIRE(preview.packets.front().image_fill.has_value());
    CHECK(preview.packets.front().image_fill->opacity == 0.75F);
    REQUIRE(session.undo(start));
    CHECK(session.selected_textured_path()->commands.size() == 2U);
    REQUIRE(session.redo(start));
    CHECK(session.selected_textured_path()->commands.size() == 3U);
    CHECK(session.update_autosave(start + std::chrono::seconds{2}) ==
          fabric::editor::AutosaveStatus::saved);

    fabric::editor::ProjectSession recovered;
    REQUIRE(recovered.open(project.path()));
    REQUIRE(recovered.select_resource(
        fabric::editor::StudioResourceKind::textured_path,
        {.value = "editable-seam-rail"}));
    REQUIRE(recovered.has_recovery());
    REQUIRE(recovered.accept_recovery(start + std::chrono::seconds{3}));
    CHECK(recovered.selected_textured_path()->commands.back().point ==
          fabric::core::Vec2{3.0F, 1.0F});
    CHECK(recovered.selected_textured_path()->uv_scale ==
          fabric::core::Vec2{8.0F, 1.5F});
    REQUIRE(recovered.save());

    fabric::editor::ProjectSession reloaded;
    REQUIRE(reloaded.open(project.path()));
    REQUIRE(reloaded.select_resource(
        fabric::editor::StudioResourceKind::textured_path,
        {.value = "editable-seam-rail"}));
    CHECK(reloaded.selected_textured_path()->commands.size() == 3U);
    CHECK(reloaded.selected_textured_path()->width == 0.25F);
    CHECK(reloaded.selected_textured_path()->opacity == 0.75F);
}

TEST_CASE("material prompt publishes and indexes a material document") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    fabric::editor::CreateMaterialPrompt prompt;
    prompt.name = "Paper fill";
    prompt.color = {0.8F, 0.7F, 0.5F, 1.0F};
    prompt.opacity = 0.75;
    prompt.blend = fabric::project::MaterialBlendMode::multiply;
    REQUIRE(session.create_material(prompt));
    REQUIRE(session.selected_resource() != nullptr);
    CHECK(session.selected_resource()->kind ==
          fabric::editor::StudioResourceKind::material);
    REQUIRE(session.selected_material().has_value());
    CHECK(session.selected_material()->document.name == "Paper fill");
    CHECK(session.selected_material()->opacity == 0.75F);
    CHECK(session.selected_material()->blend ==
          fabric::project::MaterialBlendMode::multiply);
    const auto loaded = fabric::project::load_material(
        project.path(), *session.manifest(),
        fabric::project::material_document_path(
            *session.manifest(), session.selected_material()->document.id));
    REQUIRE(loaded.ok());
    CHECK(loaded.asset->color == fabric::core::Color{0.8F, 0.7F, 0.5F, 1.0F});
}

TEST_CASE("material edits undo autosave recover and reload every property") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    const auto texture_source = project.path() / "cloth-texture.png";
    std::ofstream{texture_source, std::ios::binary} << "texture-source";
    REQUIRE(fabric::project::publish_texture_asset(
        project.path(), *session.manifest(),
        {.document = {.type = "texture",
                      .id = {.value = "cloth-texture"},
                      .name = "Cloth texture"},
         .source = "assets/textures/cloth-texture.png",
         .width = 1U,
         .height = 1U},
        texture_source).ok());
    fabric::editor::CreateVectorArtworkPrompt artwork;
    artwork.name = "Weave pattern";
    REQUIRE(session.create_vector_artwork(artwork));
    fabric::editor::CreateMaterialPrompt prompt;
    prompt.name = "Editable fill";
    REQUIRE(session.create_material(prompt));

    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    auto material = *session.selected_material();
    material.document.name = "Edited fill";
    material.color = {0.1F, 0.2F, 0.3F, 0.8F};
    material.opacity = 0.65F;
    material.blend = fabric::project::MaterialBlendMode::screen;
    material.texture = fabric::project::ResourceReference{
        {.value = "cloth-texture"}, "texture"};
    material.vector_pattern = fabric::project::ResourceReference{
        {.value = "weave-pattern"}, "vector"};
    material.uv_transform.position = {0.2F, -0.4F};
    material.uv_transform.scale = {1.5F, 0.75F};
    material.uv_transform.rotation_degrees = 32.0F;
    material.uv_transform.pivot = {0.25F, 0.6F};
    REQUIRE(session.set_selected_material(material, start));
    REQUIRE(session.dirty());
    REQUIRE(session.undo(start));
    CHECK(session.selected_material()->document.name == "Editable fill");
    REQUIRE(session.redo(start));
    CHECK(*session.selected_material() == material);
    CHECK(session.update_autosave(start + std::chrono::seconds{2}) ==
          fabric::editor::AutosaveStatus::saved);

    fabric::editor::ProjectSession recovered;
    REQUIRE(recovered.open(project.path()));
    REQUIRE(recovered.select_resource(
        fabric::editor::StudioResourceKind::material,
        {.value = "editable-fill"}));
    REQUIRE(recovered.has_recovery());
    REQUIRE(recovered.accept_recovery(start + std::chrono::seconds{3}));
    CHECK(*recovered.selected_material() == material);
    REQUIRE(recovered.save());

    fabric::editor::ProjectSession reloaded;
    REQUIRE(reloaded.open(project.path()));
    REQUIRE(reloaded.select_resource(
        fabric::editor::StudioResourceKind::material,
        {.value = "editable-fill"}));
    CHECK(*reloaded.selected_material() == material);
}

TEST_CASE("entity prompt publishes and indexes a one-node entity") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    fabric::editor::CreateEntityPrompt prompt;
    prompt.name = "Hero entity";
    prompt.node_name = "Body";
    REQUIRE(session.create_entity(prompt));
    REQUIRE(session.selected_resource() != nullptr);
    CHECK(session.selected_resource()->kind ==
          fabric::editor::StudioResourceKind::entity);
    REQUIRE(session.selected_entity().has_value());
    REQUIRE(session.selected_entity()->nodes.size() == 1U);
    CHECK(session.selected_entity()->nodes.front().name == "Body");
    CHECK(session.selected_entity()->nodes.front().drawable.kind ==
          fabric::project::EntityDrawableKind::none);
    auto node = session.selected_entity()->nodes.front();
    node.name = "Body edited";
    node.transform.position = {2.0F, -1.0F};
    node.transform.rotation_degrees = 15.0F;
    node.visible = false;
    node.locked = true;
    node.drawable = {
        .kind = fabric::project::EntityDrawableKind::vector,
        .resource = fabric::project::ResourceReference{
            {.value = "body-artwork"}, "vector"},
        .material = fabric::project::ResourceReference{
            {.value = "body-material"}, "material"}};
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    REQUIRE(session.set_selected_entity_node(0, node, start));
    CHECK(session.dirty());
    REQUIRE(session.undo());
    CHECK(session.selected_entity()->nodes.front().name == "Body");
    REQUIRE(session.redo(start));
    CHECK(session.selected_entity()->nodes.front().transform.position ==
          fabric::core::Vec2{2.0F, -1.0F});
    CHECK_FALSE(session.selected_entity()->nodes.front().visible);
    CHECK(session.selected_entity()->nodes.front().locked);
    CHECK(session.selected_entity()->nodes.front().drawable.kind ==
          fabric::project::EntityDrawableKind::vector);
    fabric::project::EntityNode child{
        .id = "child", .name = "Child", .parent = "root"};
    REQUIRE(session.add_selected_entity_node(child, start));
    REQUIRE(session.selected_entity()->nodes.size() == 2U);
    CHECK_FALSE(session.remove_selected_entity_node(0, start));
    REQUIRE(session.duplicate_selected_entity_node(0, start));
    REQUIRE(session.selected_entity()->nodes.size() == 3U);
    REQUIRE(session.move_selected_entity_node(2U, 1U, start));
    CHECK(session.selected_entity()->nodes[1].id == "root-copy");
    REQUIRE(session.undo(start));
    CHECK(session.selected_entity()->nodes[2].id == "root-copy");
    REQUIRE(session.redo(start));
    REQUIRE(session.remove_selected_entity_node(1, start));
    REQUIRE(session.remove_selected_entity_node(1, start));
    CHECK(session.selected_entity()->nodes.size() == 1U);
    CHECK(session.update_autosave(start) ==
          fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(start + std::chrono::seconds{2}) ==
          fabric::editor::AutosaveStatus::saved);
    fabric::editor::ProjectSession recovered;
    REQUIRE(recovered.open(project.path()));
    REQUIRE(recovered.select_resource(
        fabric::editor::StudioResourceKind::entity, {.value = "hero-entity"}));
    REQUIRE(recovered.has_recovery());
    REQUIRE(recovered.accept_recovery(start + std::chrono::seconds{3}));
    CHECK(recovered.selected_entity()->nodes.front().name == "Body edited");
    REQUIRE(recovered.save());
    REQUIRE(session.save());
    CHECK_FALSE(session.dirty());
    const auto loaded = fabric::project::load_entity(
        project.path(), *session.manifest(),
        fabric::project::entity_document_path(
            *session.manifest(), session.selected_entity()->document.id));
    REQUIRE(loaded.ok());
    CHECK(*loaded.entity == *session.selected_entity());
}

TEST_CASE("animation prompt publishes and indexes a clip") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    fabric::editor::CreateEntityPrompt entity_prompt;
    entity_prompt.name = "Animated hero";
    REQUIRE(session.create_entity(entity_prompt));
    fabric::editor::CreateAnimationPrompt prompt;
    prompt.name = "Walk cycle";
    prompt.preview_entity_id = "animated-hero";
    prompt.duration = 2.0;
    prompt.marker_id = "loop-point";
    prompt.marker_time = 1.5;
    REQUIRE(session.create_animation(prompt));
    REQUIRE(session.selected_resource() != nullptr);
    CHECK(session.selected_resource()->kind ==
          fabric::editor::StudioResourceKind::animation);
    REQUIRE(session.selected_animation().has_value());
    REQUIRE(session.selected_animation()->preview_entity.has_value());
    CHECK(session.selected_animation()->preview_entity->id.value ==
          "animated-hero");
    REQUIRE(session.selected_entity().has_value());
    CHECK(session.selected_animation()->duration == 2.0F);
    REQUIRE(session.selected_animation()->markers.size() == 1U);
    CHECK(session.selected_animation()->markers.front().id == "loop-point");
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    REQUIRE(session.set_selected_animation_duration(3.0F, start));
    REQUIRE(session.set_selected_animation_loop(true, start));
    REQUIRE(session.insert_selected_animation_key(
        {.node_id = "root", .component_id = "transform",
         .property_id = "position"},
        0.0F, fabric::core::Vec2{1.0F, 2.0F},
        fabric::project::AnimationInterpolation::linear, start));
    REQUIRE(session.undo(start));
    CHECK(session.selected_animation()->tracks.empty());
    REQUIRE(session.redo(start));
    REQUIRE(session.selected_animation()->tracks.size() == 1U);
    REQUIRE(session.set_selected_animation_preview_entity(std::nullopt, start));
    CHECK_FALSE(session.selected_entity().has_value());
    REQUIRE(session.undo(start));
    REQUIRE(session.selected_entity().has_value());
    REQUIRE(session.insert_selected_animation_key(
        {.node_id = "root", .component_id = "transform",
         .property_id = "position"},
        2.0F, fabric::core::Vec2{3.0F, 4.0F},
        fabric::project::AnimationInterpolation::linear, start));
    const fabric::project::PropertyBinding position_binding{
        "root", "transform", "position"};
    REQUIRE(session.move_selected_animation_key(position_binding, 1, 1.5F,
                                                start));
    REQUIRE(session.move_selected_animation_key(position_binding, 1, 1.25F,
                                                start));
    REQUIRE(session.undo(start));
    CHECK(session.selected_animation()->tracks.front().keys.back().time ==
          2.0F);
    REQUIRE(session.redo(start));
    REQUIRE(session.remove_selected_animation_key(
        position_binding,
        1U, start));
    CHECK(session.selected_animation()->tracks.front().keys.size() == 1U);
    REQUIRE(session.set_selected_animation_key(
        position_binding, 0.0F, fabric::core::Vec2{9.0F, 8.0F},
        fabric::project::AnimationInterpolation::linear, start));
    REQUIRE(session.selected_animation()->tracks.front().keys.size() == 1U);
    CHECK(std::get<fabric::core::Vec2>(
              session.selected_animation()->tracks.front().keys.front().value) ==
          fabric::core::Vec2{9.0F, 8.0F});
    REQUIRE(session.undo(start));
    CHECK(std::get<fabric::core::Vec2>(
              session.selected_animation()->tracks.front().keys.front().value) ==
          fabric::core::Vec2{1.0F, 2.0F});
    REQUIRE(session.redo(start));
    CHECK(std::get<fabric::core::Vec2>(
              session.selected_animation()->tracks.front().keys.front().value) ==
          fabric::core::Vec2{9.0F, 8.0F});
    REQUIRE(session.insert_selected_animation_marker("apex", 1.0F, start));
    CHECK_FALSE(session.insert_selected_animation_marker("apex", 1.25F, start));
    REQUIRE(session.remove_selected_animation_marker("apex", start));
    CHECK(session.selected_animation()->markers.size() == 1U);
    REQUIRE(session.update_autosave(start + std::chrono::seconds{2}) ==
            fabric::editor::AutosaveStatus::saved);
    fabric::editor::ProjectSession recovered;
    REQUIRE(recovered.open(project.path()));
    REQUIRE(recovered.select_resource(
        fabric::editor::StudioResourceKind::animation, {.value = "walk-cycle"}));
    REQUIRE(recovered.has_recovery());
    REQUIRE(recovered.accept_recovery(start + std::chrono::seconds{3}));
    REQUIRE(recovered.selected_animation()->tracks.size() == 1U);
    REQUIRE(recovered.save());
    REQUIRE(session.save());
    fabric::editor::CreateEntityPrompt other_entity_prompt;
    other_entity_prompt.name = "Unrelated selection";
    other_entity_prompt.node_name = "Root";
    REQUIRE(session.create_entity(other_entity_prompt));
    REQUIRE(session.selected_entity().has_value());
    REQUIRE(session.select_resource(
        fabric::editor::StudioResourceKind::animation, {.value = "walk-cycle"}));
    REQUIRE(session.selected_animation().has_value());
    REQUIRE(session.selected_entity().has_value());
    CHECK(session.selected_entity()->document.id.value == "animated-hero");
    const auto loaded = fabric::project::load_animation(
        project.path(), *session.manifest(),
        fabric::project::animation_document_path(
            *session.manifest(), session.selected_animation()->document.id));
    REQUIRE(loaded.ok());
    CHECK(*loaded.asset == *session.selected_animation());
}

TEST_CASE("undoing to clean neutralizes a previous autosave") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    write_project(project.path());
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    REQUIRE(session.set_project_name("Transient", start));
    REQUIRE(session.update_autosave(start + 2s) ==
            fabric::editor::AutosaveStatus::saved);
    REQUIRE(session.undo(start + 3s));
    CHECK_FALSE(session.dirty());
    REQUIRE(session.update_autosave(start + 3s) ==
            fabric::editor::AutosaveStatus::saved);
    std::filesystem::last_write_time(
        project.path() / "project.json",
        std::filesystem::file_time_type::clock::now() - 5s);

    fabric::editor::ProjectSession reopened;
    REQUIRE(reopened.open(project.path()));
    CHECK_FALSE(reopened.has_recovery());
    CHECK(reopened.manifest()->name == "Primary");
}

TEST_CASE("native vector edits use history save and recovery") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    write_project(project.path());
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    {
        fabric::editor::ProjectSession session;
        REQUIRE(session.open(project.path()));
        const auto texture_source = project.path() / "editable-fill.png";
        std::ofstream{texture_source, std::ios::binary} << "source";
        REQUIRE(fabric::project::publish_texture_asset(
            project.path(), *session.manifest(), {
                .document = {.type = "texture",
                             .id = {.value = "editable-fill"},
                             .name = "Editable Fill"},
                .source = "assets/textures/editable-fill.png",
                .width = 4, .height = 4}, texture_source).ok());
        REQUIRE(session.refresh_resources());
        fabric::editor::CreateVectorArtworkPrompt prompt;
        prompt.name = "Editable panel";
        REQUIRE(session.create_vector_artwork(prompt));
        REQUIRE(session.created_vector()->native.has_value());

        auto node = session.created_vector()->native->nodes.front();
        node.name = "Moved panel";
        node.transform.position = {3.0F, -2.0F};
        node.transform.rotation_degrees = 22.5F;
        node.transform.scale = {1.25F, 0.75F};
        node.transform.pivot = {0.5F, -0.25F};
        node.fill = {
            .kind = fabric::project::VectorFillKind::image,
            .image = fabric::project::VectorImageFill{
                .texture = {{.value = "editable-fill"}, "texture"},
                .fit = fabric::project::VectorImageFit::free,
                .transform = {.position = {0.25F, 0.5F},
                              .rotation_degrees = 15.0F,
                              .scale = {1.5F, 0.75F},
                              .pivot = {0.2F, 0.8F}},
                .opacity = 0.6F,
                .deform_with_shape = true}};
        node.stroke = fabric::project::VectorStroke{
            .color = {0.9F, 0.2F, 0.1F, 1.0F}, .width = 0.4F,
            .join = fabric::project::VectorStrokeJoin::round,
            .cap = fabric::project::VectorStrokeCap::square};
        REQUIRE(session.set_selected_vector_node(0, node, start));
        auto second = node;
        second.transform.position.x = 4.0F;
        REQUIRE(session.set_selected_vector_node(0, second, start + 1ms));
        CHECK(session.dirty());
        CHECK(session.can_undo());
        REQUIRE(session.undo(start + 1ms));
        CHECK(session.created_vector()->native->nodes.front().name ==
              "Rectangle");
        REQUIRE(session.redo(start + 2ms));
        CHECK(session.created_vector()->native->nodes.front().name ==
              "Moved panel");
        CHECK(session.created_vector()->native->nodes.front().transform ==
              second.transform);
        auto child = second;
        child.id = "second-node";
        child.name = "Second node";
        child.shape.id = "second-shape";
        child.shape.kind = fabric::project::VectorShapeKind::path;
        child.shape.path = {
            {.kind = fabric::project::VectorPathCommandKind::move,
             .point = {-1.0F, 0.0F}},
            {.kind = fabric::project::VectorPathCommandKind::cubic,
             .point = {1.0F, 0.0F}, .control1 = {-0.5F, 1.0F},
             .control2 = {0.5F, 1.0F}}};
        child.parent_id.reset();
        child.clip_node_id.reset();
        REQUIRE(session.add_selected_vector_node(child, start + 3ms));
        REQUIRE(session.set_selected_vector_node(1U, child, start + 4ms));
        REQUIRE(session.duplicate_selected_vector_node(1U, start + 5ms));
        REQUIRE(session.move_selected_vector_node(2U, 1U, start + 6ms));
        REQUIRE(session.remove_selected_vector_node(1U, start + 7ms));
        CHECK(session.created_vector()->native->nodes.size() == 2U);
        REQUIRE(session.undo(start + 8ms));
        CHECK(session.created_vector()->native->nodes.size() == 3U);
        REQUIRE(session.redo(start + 9ms));
        REQUIRE(session.update_autosave(start + 3s) ==
                fabric::editor::AutosaveStatus::saved);
    }

    const auto document =
        project.path() / "assets/vectors/editable-panel.vector.json";
    std::filesystem::last_write_time(
        document, std::filesystem::file_time_type::clock::now() - 5s);

    fabric::editor::ProjectSession recovered;
    REQUIRE(recovered.open(project.path()));
    REQUIRE(recovered.select_resource(
        fabric::editor::StudioResourceKind::vector,
        {.value = "editable-panel"}));
    REQUIRE(recovered.has_recovery());
    REQUIRE(recovered.accept_recovery(start + 3s));
    CHECK(recovered.created_vector()->native->nodes.front().name ==
          "Moved panel");
    CHECK(recovered.created_vector()->native->nodes.front().transform ==
          fabric::core::Transform{
              .position = {4.0F, -2.0F},
              .rotation_degrees = 22.5F,
              .scale = {1.25F, 0.75F},
              .pivot = {0.5F, -0.25F}});
    REQUIRE(recovered.created_vector()->native->nodes.front().fill.image);
    CHECK(recovered.created_vector()->native->nodes.front()
              .fill.image->texture.id.value == "editable-fill");
    REQUIRE(recovered.created_vector()->native->nodes.front().stroke);
    CHECK(recovered.created_vector()->native->nodes.front().stroke->join ==
          fabric::project::VectorStrokeJoin::round);
    CHECK(recovered.created_vector()->native->nodes.size() == 2U);
    CHECK(recovered.dirty());
    REQUIRE(recovered.save());
    CHECK_FALSE(recovered.dirty());

    auto saved = fabric::project::load_vector_asset(
        project.path(), *recovered.manifest(),
        "assets/vectors/editable-panel.vector.json");
    REQUIRE(saved.ok());
    CHECK(saved.asset->native->nodes.front().transform ==
          fabric::core::Transform{
              .position = {4.0F, -2.0F},
              .rotation_degrees = 22.5F,
              .scale = {1.25F, 0.75F},
              .pivot = {0.5F, -0.25F}});
}

TEST_CASE("autosave follows inactivity and leaves primary untouched") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};

    REQUIRE(session.set_project_name("Recovered", start));
    CHECK(session.update_autosave(start + 1999ms) ==
          fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(start + 2s) ==
          fabric::editor::AutosaveStatus::saved);

    CHECK(load_manifest_or_fail(project.path()).name == "Primary");
    const auto autosave_root = project.path() / ".vertex-loom/autosave";
    auto autosaved = fabric::project::load_manifest(autosave_root);
    REQUIRE(autosaved.ok());
    CHECK(autosaved.manifest->name == "Recovered");
    CHECK(session.dirty());
}

TEST_CASE("crash recovery can be declined or accepted without overwriting") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    write_project(project.path());
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    {
        fabric::editor::ProjectSession interrupted;
        REQUIRE(interrupted.open(project.path()));
        REQUIRE(interrupted.set_project_name("Recovered", start));
        REQUIRE(interrupted.update_autosave(start + 2s) ==
                fabric::editor::AutosaveStatus::saved);
    }
    std::filesystem::last_write_time(
        project.path() / "project.json",
        std::filesystem::file_time_type::clock::now() - 5s);

    fabric::editor::ProjectSession declined;
    REQUIRE(declined.open(project.path()));
    REQUIRE(declined.has_recovery());
    declined.decline_recovery();
    CHECK(declined.manifest()->name == "Primary");
    CHECK_FALSE(declined.dirty());
    CHECK(load_manifest_or_fail(project.path()).name == "Primary");

    fabric::editor::ProjectSession accepted;
    REQUIRE(accepted.open(project.path()));
    REQUIRE(accepted.has_recovery());
    REQUIRE(accepted.accept_recovery(start));
    CHECK(accepted.manifest()->name == "Recovered");
    CHECK(accepted.dirty());
    CHECK_FALSE(accepted.can_undo());
    CHECK(load_manifest_or_fail(project.path()).name == "Primary");

    REQUIRE(accepted.save());
    CHECK(load_manifest_or_fail(project.path()).name == "Recovered");
    CHECK_FALSE(accepted.dirty());
}

TEST_CASE("invalid autosave is diagnosed and never offered") {
    const TemporaryDirectory project;
    write_project(project.path());
    const auto autosave = project.path() / ".vertex-loom/autosave/project.json";
    std::filesystem::create_directories(autosave.parent_path());
    {
        std::ofstream output(autosave, std::ios::binary);
        output << "{not-json";
    }
    std::filesystem::last_write_time(
        project.path() / "project.json",
        std::filesystem::file_time_type::clock::now() -
            std::chrono::seconds{5});

    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    CHECK_FALSE(session.has_recovery());
    CHECK(session.manifest()->name == "Primary");
    CHECK_FALSE(session.errors().empty());
}

TEST_CASE("animation session accepts scalar and color key values") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    fabric::editor::CreateAnimationPrompt prompt;
    prompt.name = "Typed animation";
    prompt.generic_preview = true;
    prompt.duration = 1.0;
    REQUIRE(session.create_animation(prompt));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    const fabric::project::PropertyBinding opacity{
        "root", "material", "opacity"};
    const fabric::project::PropertyBinding tint{
        "root", "material", "color"};
    REQUIRE(session.insert_selected_animation_key(
        opacity, 0.0F, 0.0F, fabric::project::AnimationInterpolation::linear, start));
    REQUIRE(session.insert_selected_animation_key(
        opacity, 1.0F, 1.0F, fabric::project::AnimationInterpolation::linear, start));
    REQUIRE(session.insert_selected_animation_key(
        tint, 0.0F, fabric::core::Color{1.0F, 0.0F, 0.0F, 1.0F},
        fabric::project::AnimationInterpolation::linear, start));
    REQUIRE(session.insert_selected_animation_key(
        tint, 1.0F, fabric::core::Color{0.0F, 1.0F, 0.0F, 1.0F},
        fabric::project::AnimationInterpolation::linear, start));
    REQUIRE(session.selected_animation()->tracks.size() == 2U);
    CHECK(std::holds_alternative<float>(
        session.selected_animation()->tracks[0].keys.front().value));
    CHECK(std::holds_alternative<fabric::core::Color>(
        session.selected_animation()->tracks[1].keys.front().value));
}
