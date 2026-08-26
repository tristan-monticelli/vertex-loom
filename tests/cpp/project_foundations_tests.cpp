#include "fabric/core/types.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/resource_registry.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
            ("fabric-foundations-test-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

bool contains_error(const fabric::project::ValidationReport& report,
                    const fabric::project::ErrorCode code) {
    for (const auto& error : report.errors) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

fabric::project::ResourceEntry resource(
    std::string id, std::string type,
    std::vector<fabric::project::ResourceReference> references = {}) {
    const std::string path = "assets/" + id + ".json";
    return {
        .document = {
            .schema_version = 1,
            .type = std::move(type),
            .id = {.value = std::move(id)},
            .name = "Test Resource",
        },
        .document_path = path,
        .references = std::move(references),
    };
}

} // namespace

TEST_CASE("common authoring values have stable defaults") {
    const fabric::core::Vec2 vector;
    const fabric::core::Color color;
    const fabric::core::Rect rectangle;
    const fabric::core::Transform transform;

    CHECK(vector == fabric::core::Vec2{0.0F, 0.0F});
    CHECK(color == fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F});
    CHECK(rectangle.origin == fabric::core::Vec2{});
    CHECK(rectangle.size == fabric::core::Vec2{});
    CHECK(transform.position == fabric::core::Vec2{});
    CHECK(transform.rotation_degrees == 0.0F);
    CHECK(transform.scale == fabric::core::Vec2{1.0F, 1.0F});
    CHECK(transform.pivot == fabric::core::Vec2{});
}

TEST_CASE("document headers and resource references retain identity") {
    const fabric::project::DocumentHeader header{
        .schema_version = 3,
        .type = "material",
        .id = {.value = "wool.material"},
        .name = "Wool Material",
    };
    const fabric::project::ResourceReference reference{
        .id = {.value = "wool.material"},
        .expected_type = "material",
    };

    CHECK(header.id == reference.id);
    CHECK(header.type == reference.expected_type);
}

TEST_CASE("version one manifests migrate to version two without loss") {
    const auto result = fabric::project::parse_manifest(R"({
        "schemaVersion": 1,
        "id": "legacy-v1",
        "name": "Legacy V1",
        "directories": {
            "assets": "content",
            "entities": "actors",
            "maps": "levels",
            "scenes": "flows",
            "schemas": "contracts"
        }
    })");

    REQUIRE(result.ok());
    CHECK(result.manifest->schema_version == 2);
    CHECK(result.manifest->id.value == "legacy-v1");
    CHECK(result.manifest->name == "Legacy V1");
    CHECK(result.manifest->pixels_per_unit == 100.0);
    CHECK(result.manifest->directories.assets == "content");
    CHECK(result.manifest->directories.entities == "actors");
    CHECK(result.manifest->directories.maps == "levels");
    CHECK(result.manifest->directories.scenes == "flows");
    CHECK(result.manifest->directories.schemas == "contracts");
}

TEST_CASE("manifest units must be finite and positive") {
    fabric::project::ProjectManifest manifest{
        .id = {.value = "unit-test"},
        .name = "Unit Test",
    };

    manifest.pixels_per_unit = 0.0;
    CHECK_FALSE(fabric::project::validate_manifest(manifest).ok());

    manifest.pixels_per_unit = -1.0;
    CHECK_FALSE(fabric::project::validate_manifest(manifest).ok());

    manifest.pixels_per_unit =
        std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(fabric::project::validate_manifest(manifest).ok());

    manifest.pixels_per_unit = std::numeric_limits<double>::infinity();
    CHECK_FALSE(fabric::project::validate_manifest(manifest).ok());
}

TEST_CASE("version two manifest units survive serialization") {
    const fabric::project::ProjectManifest expected{
        .id = {.value = "scaled-project"},
        .name = "Scaled Project",
        .pixels_per_unit = 64.0,
    };

    const auto parsed = fabric::project::parse_manifest(
        fabric::project::serialize_manifest(expected));

    REQUIRE(parsed.ok());
    CHECK(*parsed.manifest == expected);
}

TEST_CASE("resource registry resolves unique typed references") {
    fabric::project::ResourceRegistry registry;
    REQUIRE(registry.register_resource(
                resource("wool-texture", "texture"))
                .ok());

    const fabric::project::ResourceReference reference{
        .id = {.value = "wool-texture"},
        .expected_type = "texture",
    };
    const auto* resolved = registry.resolve(reference);

    REQUIRE(resolved != nullptr);
    CHECK(resolved->document.id == reference.id);
    CHECK(registry.resolve({reference.id, "vector"}) == nullptr);
    CHECK(registry.resolve({{.value = "missing"}, "texture"}) == nullptr);
    CHECK(registry.validate().ok());
}

TEST_CASE("resource registry rejects paths outside the project") {
    fabric::project::ResourceRegistry registry;
    auto outside = resource("outside", "texture");
    outside.document_path = "../outside.texture.json";

    const auto report = registry.register_resource(std::move(outside));

    CHECK_FALSE(report.ok());
    CHECK(contains_error(report, fabric::project::ErrorCode::invalid_path));
    CHECK(registry.entries().empty());
}

TEST_CASE("resource registry rejects duplicates missing documents and types") {
    fabric::project::ResourceRegistry registry;
    REQUIRE(registry.register_resource(
                resource("shared", "texture"))
                .ok());
    REQUIRE(registry.register_resource(
                resource("shared", "vector"))
                .ok());
    REQUIRE(registry.register_resource(resource(
                "material", "material",
                {{{.value = "missing"}, "texture"},
                 {{.value = "shared"}, "texture"}}))
                .ok());
    REQUIRE(registry.register_resource(
                resource("vector-only", "vector"))
                .ok());
    REQUIRE(registry.register_resource(resource(
                "typed-source", "material",
                {{{.value = "vector-only"}, "texture"}}))
                .ok());

    const auto report = registry.validate();
    CHECK(contains_error(report,
                         fabric::project::ErrorCode::duplicate_resource));
    CHECK(contains_error(report,
                         fabric::project::ErrorCode::missing_resource));
    CHECK(contains_error(
        report, fabric::project::ErrorCode::resource_type_mismatch));
}

TEST_CASE("resource registry rejects dependency cycles") {
    fabric::project::ResourceRegistry registry;
    REQUIRE(registry.register_resource(resource(
                "cycle-a", "entity",
                {{{.value = "cycle-b"}, "entity"}}))
                .ok());
    REQUIRE(registry.register_resource(resource(
                "cycle-b", "entity",
                {{{.value = "cycle-c"}, "entity"}}))
                .ok());
    REQUIRE(registry.register_resource(resource(
                "cycle-c", "entity",
                {{{.value = "cycle-a"}, "entity"}}))
                .ok());

    CHECK(contains_error(registry.validate(),
                         fabric::project::ErrorCode::resource_cycle));
}

TEST_CASE("headless project validation rejects duplicate resource identifiers") {
    const TemporaryDirectory temporary;
    const fabric::project::ProjectManifest manifest{
        .id = {.value = "registry-project"},
        .name = "Registry Project",
    };
    REQUIRE(fabric::project::create_project(temporary.path(), manifest).ok());

    const fabric::project::TextureAsset texture{
        .document = {
            .type = "texture",
            .id = {.value = "shared-resource"},
            .name = "Shared Texture",
        },
        .source = "assets/textures/shared-resource.png",
        .width = 1,
        .height = 1,
    };
    const fabric::project::VectorAsset vector{
        .document = {
            .schema_version = fabric::project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = "shared-resource"},
            .name = "Shared Vector",
        },
        .source_kind = fabric::project::VectorSourceKind::linked_svg,
        .source = "assets/vectors/shared-resource.svg",
    };
    {
        std::ofstream texture_source(
            temporary.path() / texture.source, std::ios::binary);
        texture_source << "source";
        std::ofstream texture_document(
            temporary.path() /
                "assets/textures/shared-resource.texture.json",
            std::ios::binary);
        texture_document << fabric::project::serialize_texture_asset(texture);
        std::ofstream vector_source(
            temporary.path() / vector.source, std::ios::binary);
        vector_source << "<svg/>";
        std::ofstream vector_document(
            temporary.path() /
                "assets/vectors/shared-resource.vector.json",
            std::ios::binary);
        vector_document << fabric::project::serialize_vector_asset(vector);
    }

    const auto report = fabric::project::validate_project(temporary.path());
    CHECK(contains_error(report,
                         fabric::project::ErrorCode::duplicate_resource));
}

TEST_CASE("headless project validation accepts a native-only vector asset") {
    const TemporaryDirectory temporary;
    const fabric::project::ProjectManifest manifest{
        .id = {.value = "native-only-project"},
        .name = "Native Only Project",
    };
    REQUIRE(fabric::project::create_project(temporary.path(), manifest).ok());
    const auto texture_input = temporary.path() / "fill-input.png";
    std::ofstream{texture_input, std::ios::binary} << "texture-source";
    const fabric::project::TextureAsset texture{
        .document = {
            .type = "texture",
            .id = {.value = "panel-fill"},
            .name = "Panel Fill",
        },
        .source = "assets/textures/panel-fill.png",
        .width = 1,
        .height = 1,
    };
    REQUIRE(fabric::project::publish_texture_asset(
                temporary.path(), manifest, texture, texture_input)
                .ok());
    const fabric::project::VectorAsset vector{
        .document = {
            .schema_version = fabric::project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = "native-panel"},
            .name = "Native Panel",
        },
        .source_kind = fabric::project::VectorSourceKind::native,
        .native = fabric::project::NativeVectorDefinition{
            .size = {10.0F, 10.0F},
            .nodes = {{
                .id = "node-1",
                .name = "Panel",
                .shape = {
                    .id = "shape-1",
                    .bounds = {.origin = {-5.0F, -5.0F},
                               .size = {10.0F, 10.0F}},
                },
                .fill = {
                    .kind = fabric::project::VectorFillKind::image,
                    .image = fabric::project::VectorImageFill{
                        .texture = {{.value = "panel-fill"}, "texture"},
                        .fit = fabric::project::VectorImageFit::cover,
                        .deform_with_shape = true,
                    },
                },
            }},
        },
    };
    REQUIRE(fabric::project::publish_native_vector_asset(
                temporary.path(), manifest, vector)
                .ok());

    CHECK(fabric::project::validate_project(temporary.path()).ok());
}

TEST_CASE("headless project validation rejects a missing image fill texture") {
    const TemporaryDirectory temporary;
    const fabric::project::ProjectManifest manifest{
        .id = {.value = "missing-fill-project"},
        .name = "Missing Fill Project",
    };
    REQUIRE(fabric::project::create_project(temporary.path(), manifest).ok());
    const fabric::project::VectorAsset vector{
        .document = {
            .schema_version = fabric::project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = "missing-fill-panel"},
            .name = "Missing Fill Panel",
        },
        .source_kind = fabric::project::VectorSourceKind::native,
        .native = fabric::project::NativeVectorDefinition{
            .size = {1.0F, 1.0F},
            .nodes = {{
                .id = "node-1",
                .name = "Panel",
                .shape = {
                    .id = "shape-1",
                    .bounds = {.size = {1.0F, 1.0F}},
                },
                .fill = {
                    .kind = fabric::project::VectorFillKind::image,
                    .image = fabric::project::VectorImageFill{
                        .texture = {{.value = "missing-texture"}, "texture"},
                    },
                },
            }},
        },
    };
    REQUIRE(fabric::project::publish_native_vector_asset(
                temporary.path(), manifest, vector)
                .ok());

    const auto report = fabric::project::validate_project(temporary.path());
    CHECK(contains_error(report,
                         fabric::project::ErrorCode::missing_resource));
}

TEST_CASE("vector primitives convert to editable path commands") {
    using Kind = fabric::project::VectorPathCommandKind;
    const fabric::project::VectorShape rectangle{
        .kind = fabric::project::VectorShapeKind::rectangle,
        .bounds = {{-2.0F, -1.0F}, {4.0F, 2.0F}}};
    const auto rectangle_path =
        fabric::project::path_commands_from_shape(rectangle);
    REQUIRE(rectangle_path.has_value());
    REQUIRE(rectangle_path->size() == 5U);
    CHECK(rectangle_path->front().kind == Kind::move);
    CHECK(rectangle_path->back().kind == Kind::close);
    CHECK(rectangle_path->at(1).point == fabric::core::Vec2{2.0F, -1.0F});

    const fabric::project::VectorShape line{
        .kind = fabric::project::VectorShapeKind::line,
        .points = {{0.0F, 0.0F}, {1.0F, 2.0F}, {3.0F, 1.0F}}};
    const auto line_path = fabric::project::path_commands_from_shape(line);
    REQUIRE(line_path.has_value());
    REQUIRE(line_path->size() == 3U);
    CHECK(line_path->at(0).kind == Kind::move);
    CHECK(line_path->at(2).point == fabric::core::Vec2{3.0F, 1.0F});

    const fabric::project::VectorShape ellipse{
        .kind = fabric::project::VectorShapeKind::ellipse,
        .bounds = {{-2.0F, -1.0F}, {4.0F, 2.0F}}};
    const auto ellipse_path =
        fabric::project::path_commands_from_shape(ellipse);
    REQUIRE(ellipse_path.has_value());
    REQUIRE(ellipse_path->size() == 6U);
    CHECK(std::ranges::count_if(*ellipse_path, [](const auto& command) {
              return command.kind == Kind::cubic;
          }) == 4U);

    const fabric::project::VectorShape degenerate_line{
        .kind = fabric::project::VectorShapeKind::line,
        .points = {{0.0F, 0.0F}}};
    CHECK_FALSE(fabric::project::path_commands_from_shape(degenerate_line));

    fabric::project::VectorShape editable_path{
        .kind = fabric::project::VectorShapeKind::path,
        .path = {{.kind = Kind::move, .point = {0.0F, 0.0F}},
                 {.kind = Kind::line, .point = {1.0F, 0.0F}}}};
    REQUIRE(fabric::project::insert_path_command(
        editable_path, 1U,
        {.kind = Kind::cubic,
         .point = {2.0F, 1.0F},
         .control1 = {1.0F, 0.5F},
         .control2 = {1.5F, 1.0F}}));
    REQUIRE(editable_path.path.size() == 3U);
    CHECK(editable_path.path[1].kind == Kind::cubic);
    REQUIRE(fabric::project::remove_path_command(editable_path, 1U));
    CHECK(editable_path.path.size() == 2U);
    CHECK_FALSE(fabric::project::remove_path_command(editable_path, 0U));
    CHECK_FALSE(fabric::project::insert_path_command(
        editable_path, 2U, {.kind = Kind::move, .point = {2.0F, 0.0F}}));

    REQUIRE(fabric::project::insert_path_command(
        editable_path, 1U,
        {.kind = Kind::line, .point = {0.5F, 0.25F}}));
    CHECK(editable_path.path[1].point == fabric::core::Vec2{0.5F, 0.25F});
    REQUIRE(fabric::project::remove_path_command(editable_path, 1U));
    CHECK(editable_path.path[1].point == fabric::core::Vec2{1.0F, 0.0F});
}
