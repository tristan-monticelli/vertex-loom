#include "fabric/core/types.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

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
