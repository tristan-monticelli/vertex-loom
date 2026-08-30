#include "fabric/editor/visual_presets.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/render/visual_composition_renderer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <tuple>

namespace {

class TemporaryProject {
public:
    explicit TemporaryProject(const std::string& prefix) {
        root_ = std::filesystem::temp_directory_path() /
            (prefix + "-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(fabric::project::create_project(root_, manifest()).ok());
    }

    ~TemporaryProject() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    static fabric::project::ProjectManifest manifest() {
        return {.schema_version = fabric::project::current_schema_version,
                .id = {.value = "composition-render-tests"},
                .name = "Composition Render Tests"};
    }

private:
    std::filesystem::path root_;
};

void publish_thread_texture(const std::filesystem::path& root) {
    const auto source = root / "thread.png";
    std::ofstream{source, std::ios::binary} << "thread-source";
    REQUIRE(fabric::project::publish_texture_asset(
        root, TemporaryProject::manifest(),
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "cotton-thread"},
                      .name = "Cotton Thread"},
         .source = "assets/textures/cotton-thread.png",
         .width = 8U,
         .height = 8U},
        source).ok());
}

fabric::editor::VisualPresetRequest request(
    const fabric::editor::VisualPresetKind kind, const std::string& id) {
    return {.kind = kind,
            .id = {.value = id},
            .name = std::string{fabric::editor::label(kind)},
            .thread_texture = fabric::project::ResourceReference{
                {.value = "cotton-thread"}, "texture"},
            .zipper_tooth_count = 10U};
}

fabric::project::VisualComponent load_component(
    const TemporaryProject& project, const std::string& id) {
    auto loaded = fabric::project::load_visual_component(
        project.root(), TemporaryProject::manifest(),
        fabric::project::visual_component_document_path(
            TemporaryProject::manifest(), {.value = id}));
    REQUIRE(loaded.ok());
    return std::move(*loaded.asset);
}

} // namespace

TEST_CASE("visual preset components resolve to shared draw packets") {
    const TemporaryProject project{"fabric-composition-render"};
    publish_thread_texture(project.root());
    for (const auto [kind, id, expected_packets] : {
             std::tuple{fabric::editor::VisualPresetKind::eye,
                        "preview-eye", 4U},
             std::tuple{fabric::editor::VisualPresetKind::button,
                        "preview-button", 5U},
             std::tuple{fabric::editor::VisualPresetKind::seam,
                        "preview-seam", 2U},
             std::tuple{fabric::editor::VisualPresetKind::zipper,
                        "preview-zipper", 14U}}) {
        REQUIRE(fabric::editor::publish_visual_preset(
            project.root(), TemporaryProject::manifest(),
            request(kind, id)).ok());
        const auto component = load_component(project, id);
        const auto resolved = fabric::render::resolve_visual_component(
            project.root(), TemporaryProject::manifest(), component);
        INFO(id);
        CHECK(resolved.ok());
        CHECK(resolved.packets.size() == expected_packets);
        CHECK(resolved.bounds.size.x > 0.0F);
        CHECK(resolved.bounds.size.y > 0.0F);
        if (kind == fabric::editor::VisualPresetKind::zipper) {
            CHECK(resolved.packets.front().node_id.find(":left-rail:") !=
                  std::string::npos);
            CHECK(resolved.packets.back().node_id.find(":slider:") !=
                  std::string::npos);
        }
    }
}

TEST_CASE("component parameters transform their generic target layer") {
    const TemporaryProject project{"fabric-component-parameter-render"};
    publish_thread_texture(project.root());
    REQUIRE(fabric::editor::publish_visual_preset(
        project.root(), TemporaryProject::manifest(),
        request(fabric::editor::VisualPresetKind::zipper,
                "moving-zipper")).ok());
    auto component = load_component(project, "moving-zipper");
    const auto base = fabric::render::resolve_visual_component(
        project.root(), TemporaryProject::manifest(), component);
    const auto moved = fabric::render::resolve_visual_component(
        project.root(), TemporaryProject::manifest(), component,
        {.overrides = {{"slider-position", fabric::core::Vec2{2.0F, 3.0F}}}});
    REQUIRE(base.ok());
    REQUIRE(moved.ok());
    const auto find_slider = [](const auto& packets) {
        return std::ranges::find_if(packets, [](const auto& packet) {
            return packet.node_id.find(":slider:body") != std::string::npos;
        });
    };
    const auto base_slider = find_slider(base.packets);
    const auto moved_slider = find_slider(moved.packets);
    REQUIRE(base_slider != base.packets.end());
    REQUIRE(moved_slider != moved.packets.end());
    REQUIRE_FALSE(base_slider->fill_vertices.empty());
    REQUIRE_FALSE(moved_slider->fill_vertices.empty());
    CHECK(moved_slider->fill_vertices.front().x ==
          Catch::Approx(base_slider->fill_vertices.front().x + 2.0F));
    CHECK(moved_slider->fill_vertices.front().y ==
          Catch::Approx(base_slider->fill_vertices.front().y + 3.0F));

    component.anchors.front().position = {1.0F, 2.0F};
    const auto anchored = fabric::render::resolve_visual_component(
        project.root(), TemporaryProject::manifest(), component,
        {.anchor_id = "center"});
    REQUIRE(anchored.ok());
    const auto anchored_slider = find_slider(anchored.packets);
    REQUIRE(anchored_slider != anchored.packets.end());
    CHECK(anchored_slider->fill_vertices.front().x ==
          Catch::Approx(base_slider->fill_vertices.front().x - 1.0F));
    CHECK(anchored_slider->fill_vertices.front().y ==
          Catch::Approx(base_slider->fill_vertices.front().y - 2.0F));

    component.parameters.front().target.property_id = "unsupported";
    const auto unsupported = fabric::render::resolve_visual_component(
        project.root(), TemporaryProject::manifest(), component);
    CHECK_FALSE(unsupported.ok());
    CHECK(std::ranges::any_of(unsupported.errors, [](const auto& error) {
        return error.find("unsupported visual parameter target") !=
            std::string::npos;
    }));
}

TEST_CASE("textured layer opacity is applied exactly once") {
    const TemporaryProject project{"fabric-composition-opacity"};
    publish_thread_texture(project.root());
    REQUIRE(fabric::editor::publish_visual_preset(
        project.root(), TemporaryProject::manifest(),
        request(fabric::editor::VisualPresetKind::seam,
                "faded-seam")).ok());
    auto loaded = fabric::project::load_visual_composition(
        project.root(), TemporaryProject::manifest(),
        fabric::project::visual_composition_document_path(
            TemporaryProject::manifest(),
            {.value = "faded-seam-composition"}));
    REQUIRE(loaded.ok());
    loaded.asset->layers.front().opacity = 0.5F;
    const auto resolved = fabric::render::resolve_visual_composition(
        project.root(), TemporaryProject::manifest(), *loaded.asset);
    REQUIRE(resolved.ok());
    REQUIRE(resolved.packets.size() == 2U);
    REQUIRE(resolved.packets.front().image_fill.has_value());
    REQUIRE(resolved.packets.front().fill_color.has_value());
    CHECK(resolved.packets.front().image_fill->opacity == Catch::Approx(0.5F));
    CHECK(resolved.packets.front().fill_color->alpha == Catch::Approx(1.0F));
    REQUIRE(resolved.packets.back().stroke.has_value());
    CHECK(resolved.packets.back().stroke->width == Catch::Approx(0.06F));
}

TEST_CASE("animated component parameters rebuild Beam packets generically") {
    const TemporaryProject project{"fabric-animated-beam"};
    publish_thread_texture(project.root());
    REQUIRE(fabric::editor::publish_visual_preset(
        project.root(), TemporaryProject::manifest(),
        request(fabric::editor::VisualPresetKind::seam, "beam")).ok());
    const auto component = load_component(project, "beam");
    const auto base = fabric::render::resolve_visual_component(
        project.root(), TemporaryProject::manifest(), component);
    const fabric::project::EvaluationResult evaluation{
        .properties = {
            {{"beam-node", "beam", "width"}, 0.5F},
            {{"beam-node", "beam", "offset"}, 2.0F},
            {{"beam-node", "beam", "color"},
             fabric::core::Color{0.2F, 0.4F, 0.8F, 1.0F}},
            {{"other-node", "beam", "offset"}, 9.0F}}};
    const auto animated = fabric::render::resolve_animated_visual_component(
        project.root(), TemporaryProject::manifest(), component, {},
        "beam-node", evaluation);
    REQUIRE(base.ok());
    REQUIRE(animated.ok());
    REQUIRE(base.packets.size() == 2U);
    REQUIRE(animated.packets.size() == 2U);
    REQUIRE_FALSE(base.packets.front().fill_uv.empty());
    REQUIRE_FALSE(animated.packets.front().fill_uv.empty());
    CHECK(animated.packets.front().fill_uv.front().x == Catch::Approx(2.0F));
    CHECK(animated.packets.front().fill_color ==
          fabric::core::Color{0.2F, 0.4F, 0.8F, 1.0F});
    REQUIRE(base.packets.back().stroke.has_value());
    REQUIRE(animated.packets.back().stroke.has_value());
    CHECK(base.packets.back().stroke->width == Catch::Approx(0.06F));
    CHECK(base.packets.back().stroke->join ==
          fabric::project::VectorStrokeJoin::round);
    CHECK(base.packets.back().stroke->cap ==
          fabric::project::VectorStrokeCap::round);
    CHECK(animated.bounds.size.y > base.bounds.size.y);
}

TEST_CASE("visual resolver reports missing textures and component cycles") {
    const TemporaryProject missing{"fabric-composition-missing-texture"};
    REQUIRE(fabric::editor::publish_visual_preset(
        missing.root(), TemporaryProject::manifest(),
        request(fabric::editor::VisualPresetKind::seam,
                "missing-thread-seam")).ok());
    const auto missing_result = fabric::render::resolve_visual_component(
        missing.root(), TemporaryProject::manifest(),
        load_component(missing, "missing-thread-seam"));
    CHECK_FALSE(missing_result.ok());

    const TemporaryProject cyclic{"fabric-composition-cycle"};
    const auto manifest = TemporaryProject::manifest();
    const fabric::project::VisualComposition composition_a{
        .document = {.schema_version = 1, .type = "visualComposition",
                     .id = {.value = "composition-a"}, .name = "A"},
        .layers = {{.id = "b", .name = "B",
                    .kind = fabric::project::VisualLayerKind::component,
                    .resource = {{.value = "component-b"},
                                 "visualComponent"},
                    .component_instance =
                        fabric::project::VisualComponentInstance{}}}};
    const fabric::project::VisualComposition composition_b{
        .document = {.schema_version = 1, .type = "visualComposition",
                     .id = {.value = "composition-b"}, .name = "B"},
        .layers = {{.id = "a", .name = "A",
                    .kind = fabric::project::VisualLayerKind::component,
                    .resource = {{.value = "component-a"},
                                 "visualComponent"},
                    .component_instance =
                        fabric::project::VisualComponentInstance{}}}};
    REQUIRE(fabric::project::publish_visual_composition(
        cyclic.root(), manifest, composition_a).ok());
    REQUIRE(fabric::project::publish_visual_composition(
        cyclic.root(), manifest, composition_b).ok());
    REQUIRE(fabric::project::publish_visual_component(
        cyclic.root(), manifest,
        {.document = {.schema_version = 1, .type = "visualComponent",
                      .id = {.value = "component-a"}, .name = "A"},
         .composition = {{.value = "composition-a"},
                         "visualComposition"}}).ok());
    REQUIRE(fabric::project::publish_visual_component(
        cyclic.root(), manifest,
        {.document = {.schema_version = 1, .type = "visualComponent",
                      .id = {.value = "component-b"}, .name = "B"},
         .composition = {{.value = "composition-b"},
                         "visualComposition"}}).ok());
    const auto cycle_result = fabric::render::resolve_visual_component(
        cyclic.root(), manifest, load_component(cyclic, "component-a"));
    CHECK_FALSE(cycle_result.ok());
    CHECK(std::ranges::any_of(cycle_result.errors, [](const auto& error) {
        return error.find("cycle") != std::string::npos;
    }));
}
