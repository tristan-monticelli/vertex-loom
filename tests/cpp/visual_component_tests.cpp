#include "fabric/project/texture_asset.hpp"
#include "fabric/project/visual_component.hpp"
#include "fabric/project/visual_composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "component-tests"},
            .name = "Component Tests"};
}

fabric::project::PropertyBinding target(const std::string& component,
                                        const std::string& property) {
    return {.node_id = "face",
            .component_id = component,
            .property_id = property};
}

fabric::project::VisualComponent component() {
    using Type = fabric::project::VisualParameterType;
    return {
        .document = {.schema_version = 1,
                     .type = "visualComponent",
                     .id = {.value = "button-eye"},
                     .name = "Button Eye"},
        .composition = {{.value = "button-eye-base"}, "visualComposition"},
        .bounds = {{-1.0F, -0.75F}, {2.0F, 1.5F}},
        .anchors = {{"center", "Center", {0.0F, 0.0F}},
                    {"thread", "Thread", {0.0F, 0.25F}}},
        .parameters = {
            {"size", "Size", Type::vec2, fabric::core::Vec2{1.0F, 1.0F},
             target("transform", "scale"), true},
            {"rotation", "Rotation", Type::angle, 0.0F,
             target("transform", "rotationDegrees"), true},
            {"holes", "Holes", Type::integer, std::int64_t{4},
             target("component", "holes"), false},
            {"visible", "Visible", Type::boolean, true,
             target("layer", "visible"), true},
            {"label", "Label", Type::text, std::string{"button"},
             target("component", "label"), false},
            {"offset", "Offset", Type::scalar, 0.0F,
             target("layer", "opacity"), true},
            {"tint", "Tint", Type::color,
             fabric::core::Color{0.8F, 0.4F, 0.2F, 1.0F},
             target("material", "color"), true},
            {"fabric", "Fabric", Type::resource,
             fabric::project::ResourceReference{
                 {.value = "face-source"}, "texture"},
             target("layer", "resource"), true},
        },
        .variants = {{"sleepy", "Sleepy",
                      {{"size", fabric::core::Vec2{1.2F, 0.6F}},
                       {"visible", true}}}},
    };
}

fabric::project::VisualComposition base_composition() {
    return {
        .document = {.schema_version = 1,
                     .type = "visualComposition",
                     .id = {.value = "button-eye-base"},
                     .name = "Button Eye Base"},
        .size = {2.0F, 1.5F},
        .layers = {{.id = "face",
                    .name = "Face",
                    .kind = fabric::project::VisualLayerKind::raster,
                    .resource = {{.value = "face-source"}, "texture"}}},
    };
}

fabric::project::VisualComposition parent_composition() {
    return {
        .document = {.schema_version = 1,
                     .type = "visualComposition",
                     .id = {.value = "textile-face"},
                     .name = "Textile Face"},
        .size = {4.0F, 3.0F},
        .layers = {{.id = "left-eye",
                    .name = "Left Eye",
                    .kind = fabric::project::VisualLayerKind::component,
                    .resource = {{.value = "button-eye"}, "visualComponent"},
                    .component_instance = fabric::project::VisualComponentInstance{
                        .variant_id = "sleepy",
                        .anchor_id = "center",
                        .overrides = {{"size", fabric::core::Vec2{1.4F, 0.7F}}}}}},
    };
}

std::filesystem::path temporary_root(const std::string& prefix) {
    return std::filesystem::temp_directory_path() /
        (prefix + "-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

bool has_error(const fabric::project::ValidationReport& report,
               const fabric::project::ErrorCode code) {
    return std::ranges::any_of(report.errors, [&](const auto& error) {
        return error.code == code;
    });
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void publish_texture(const std::filesystem::path& root) {
    const auto input = root / "input.png";
    std::ofstream{input, std::ios::binary} << "source-bytes";
    REQUIRE(fabric::project::publish_texture_asset(
        root, manifest(),
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "face-source"},
                      .name = "Face Source"},
         .source = "assets/textures/face-source.png",
         .width = 16U,
         .height = 12U},
        input).ok());
}

} // namespace

TEST_CASE("visual component v1 round trips typed parameters and variants") {
    const auto source = component();
    const auto parsed = fabric::project::parse_visual_component(
        manifest(), fabric::project::serialize_visual_component(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);
    const auto references =
        fabric::project::visual_component_resource_references(source);
    CHECK(std::ranges::any_of(references, [](const auto& reference) {
        return reference.id.value == "button-eye-base" &&
            reference.expected_type == "visualComposition";
    }));
    CHECK(std::ranges::any_of(references, [](const auto& reference) {
        return reference.id.value == "face-source" &&
            reference.expected_type == "texture";
    }));
}

TEST_CASE("component instances resolve default variant then instance values") {
    const auto source = component();
    const fabric::project::VisualComponentInstance instance{
        .variant_id = "sleepy",
        .anchor_id = "thread",
        .overrides = {{"size", fabric::core::Vec2{1.5F, 0.8F}},
                      {"rotation", 15.0F}},
    };
    const auto resolved = fabric::project::resolve_visual_component_instance(
        source, instance);
    REQUIRE(resolved.ok());
    const auto size = std::ranges::find_if(resolved.parameters,
        [](const auto& parameter) { return parameter.id == "size"; });
    REQUIRE(size != resolved.parameters.end());
    CHECK(std::get<fabric::core::Vec2>(size->value) ==
          fabric::core::Vec2{1.5F, 0.8F});

    fabric::project::PropertyDescriptorRegistry registry;
    const auto descriptors =
        fabric::project::visual_component_property_descriptors(source);
    REQUIRE(descriptors.size() == 8U);
    const auto holes = std::ranges::find_if(descriptors,
        [](const auto& descriptor) { return descriptor.property_id == "holes"; });
    const auto label = std::ranges::find_if(descriptors,
        [](const auto& descriptor) { return descriptor.property_id == "label"; });
    REQUIRE(holes != descriptors.end());
    REQUIRE(label != descriptors.end());
    CHECK(holes->value_kind == fabric::project::PropertyValueKind::integer);
    CHECK(label->value_kind == fabric::project::PropertyValueKind::text);
    for (const auto& descriptor : descriptors) {
        REQUIRE(registry.register_descriptor(descriptor).ok());
    }
    CHECK(registry.animatable().size() == 6U);
}

TEST_CASE("visual component rejects invalid declarations and instances") {
    auto invalid = component();
    invalid.anchors[1].id = invalid.anchors[0].id;
    invalid.parameters[2].animatable = true;
    invalid.variants.front().overrides.front().value = true;
    const auto validation = fabric::project::validate_visual_component(
        manifest(), invalid);
    CHECK_FALSE(validation.ok());
    CHECK(validation.errors.size() >= 3U);

    const auto unresolved = fabric::project::resolve_visual_component_instance(
        component(), {.variant_id = "missing", .anchor_id = "missing"});
    CHECK_FALSE(unresolved.ok());
    CHECK(unresolved.errors.size() == 2U);

    const auto unknown = fabric::project::parse_visual_component(
        manifest(),
        R"({"schemaVersion":1,"type":"visualComponent","id":"bad","name":"Bad","composition":{"id":"base","expectedType":"visualComposition"},"bounds":{"origin":{"x":0,"y":0},"size":{"x":1,"y":1}},"anchors":[],"parameters":[],"variants":[],"surprise":true})");
    CHECK_FALSE(unknown.ok());
}

TEST_CASE("visual component publication preserves the last valid document") {
    const auto root = temporary_root("fabric-component-publish");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source = component();
    REQUIRE(fabric::project::publish_visual_component(
                root, manifest(), source).ok());
    const auto path = root / fabric::project::visual_component_document_path(
        manifest(), source.document.id);
    const auto valid_contents = read_file(path);
    source.bounds.size.x = -1.0F;
    CHECK_FALSE(fabric::project::publish_visual_component(
                    root, manifest(), source).ok());
    CHECK(read_file(path) == valid_contents);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("headless validation checks component bindings instances and cycles") {
    const auto root = temporary_root("fabric-component-graph");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    publish_texture(root);
    auto base = base_composition();
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), base).ok());
    auto source = component();
    REQUIRE(fabric::project::publish_visual_component(
                root, manifest(), source).ok());
    auto parent = parent_composition();
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), parent).ok());
    CHECK(fabric::project::validate_project(root).ok());

    parent.layers.front().component_instance->variant_id = "missing";
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), parent).ok());
    CHECK(has_error(fabric::project::validate_project(root),
                    fabric::project::ErrorCode::missing_resource));

    parent.layers.front().component_instance->variant_id = "sleepy";
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), parent).ok());
    source.parameters.front().target.node_id = "missing-layer";
    REQUIRE(fabric::project::publish_visual_component(
                root, manifest(), source).ok());
    CHECK(has_error(fabric::project::validate_project(root),
                    fabric::project::ErrorCode::missing_resource));

    source.parameters.front().target.node_id = "face";
    REQUIRE(fabric::project::publish_visual_component(
                root, manifest(), source).ok());
    base.layers.push_back({
        .id = "recursive-eye",
        .name = "Recursive Eye",
        .kind = fabric::project::VisualLayerKind::component,
        .resource = {{.value = "button-eye"}, "visualComponent"},
        .component_instance = fabric::project::VisualComponentInstance{},
    });
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), base).ok());
    CHECK(has_error(fabric::project::validate_project(root),
                    fabric::project::ErrorCode::resource_cycle));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
