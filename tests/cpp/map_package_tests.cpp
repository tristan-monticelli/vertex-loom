#include "fabric/project/map_package.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

fabric::project::MapPackageManifest package_manifest() {
    using fabric::project::MapPackageResource;
    using fabric::project::ResourceReference;
    const ResourceReference map{{.value = "rotating-platform"}, "map"};
    return {
        .schema_version = 1,
        .type = "map-package",
        .id = {.value = "rotating-platform-package"},
        .name = "Rotating Platform",
        .minimum_runtime_version = "0.1.0",
        .root_map = map,
        .resources = {
            MapPackageResource{
                .resource = {{.value = "platform-entity"}, "entity"},
                .document_path = "entities/platform-entity.entity.json"},
            MapPackageResource{
                .resource = map,
                .document_path = "maps/rotating-platform.map.json"},
            MapPackageResource{
                .resource = {{.value = "yarn-fill"}, "texture"},
                .document_path = "assets/textures/yarn-fill.texture.json",
                .payload_paths = {"assets/textures/yarn-fill.png"}},
        }};
}

std::filesystem::path fixture(const std::string_view name) {
    return std::filesystem::path{__FILE__}.parent_path().parent_path() /
        "fixtures" / name;
}

} // namespace

TEST_CASE("map package manifests round trip deterministically") {
    const auto expected = package_manifest();
    const auto serialized =
        fabric::project::serialize_map_package_manifest(expected);
    const auto parsed = fabric::project::parse_map_package_manifest(serialized);
    REQUIRE(parsed.ok());
    CHECK(*parsed.manifest == expected);
    CHECK(fabric::project::serialize_map_package_manifest(*parsed.manifest) ==
          serialized);
}

TEST_CASE("map package compatibility uses strict semantic versions") {
    const auto package = package_manifest();
    CHECK(fabric::project::runtime_can_load_map_package(package));
    CHECK(fabric::project::runtime_can_load_map_package(package, "0.1.0"));
    CHECK(fabric::project::runtime_can_load_map_package(package, "0.2.0"));
    CHECK_FALSE(
        fabric::project::runtime_can_load_map_package(package, "0.0.9"));
    CHECK_FALSE(
        fabric::project::runtime_can_load_map_package(package, "0.1"));

    auto invalid = package;
    invalid.minimum_runtime_version = "00.1.0";
    CHECK_FALSE(fabric::project::validate_map_package_manifest(invalid).ok());
}

TEST_CASE("map package manifests reject nonportable and colliding paths") {
    auto invalid = package_manifest();
    invalid.resources[0].document_path = "../outside.entity.json";
    REQUIRE_FALSE(
        fabric::project::validate_map_package_manifest(invalid).ok());

    invalid = package_manifest();
    invalid.resources[0].document_path = "/outside.entity.json";
    REQUIRE_FALSE(
        fabric::project::validate_map_package_manifest(invalid).ok());

    invalid = package_manifest();
    invalid.resources[0].document_path =
        "C:\\outside\\platform.entity.json";
    REQUIRE_FALSE(
        fabric::project::validate_map_package_manifest(invalid).ok());

    invalid = package_manifest();
    invalid.resources[2].payload_paths = {
        invalid.resources[1].document_path};
    REQUIRE_FALSE(
        fabric::project::validate_map_package_manifest(invalid).ok());

    invalid = package_manifest();
    std::swap(invalid.resources[0], invalid.resources[1]);
    REQUIRE_FALSE(
        fabric::project::validate_map_package_manifest(invalid).ok());
}

TEST_CASE("map package parser rejects unknown fields and a missing root") {
    auto unknown = fabric::project::serialize_map_package_manifest(
        package_manifest());
    unknown.insert(unknown.find('\n') + 1, "  \"extra\": true,\n");
    CHECK_FALSE(fabric::project::parse_map_package_manifest(unknown).ok());

    auto invalid = package_manifest();
    invalid.root_map.id.value = "another-map";
    CHECK_FALSE(fabric::project::validate_map_package_manifest(invalid).ok());
}

TEST_CASE("map package planning closes the rotating platform graph") {
    const auto planned = fabric::project::plan_map_package(
        fixture("studio-rotating-platform"), {.value = "platform-preview"});
    REQUIRE(planned.ok());
    const std::vector<std::pair<std::string, std::string>> expected{
        {"entity", "platform-visual"},
        {"map", "platform-preview"},
        {"mechanic", "rotating-platform"},
        {"texture", "platform-thread"},
        {"texturedPath", "platform-strip-rail"},
        {"visualComponent", "platform-strip"},
        {"visualComposition", "platform-strip-composition"},
    };
    REQUIRE(planned.manifest->resources.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK(planned.manifest->resources[index].resource.expected_type ==
              expected[index].first);
        CHECK(planned.manifest->resources[index].resource.id.value ==
              expected[index].second);
    }
    CHECK(planned.manifest->resources[3].payload_paths ==
          std::vector<std::filesystem::path>{
              "assets/textures/platform-thread.png"});
    CHECK(std::ranges::count_if(
              planned.manifest->resources, [](const auto& resource) {
                  return resource.document_path ==
                      "maps/platform-preview.map.json";
              }) == 1);
    const auto repeated = fabric::project::plan_map_package(
        fixture("studio-rotating-platform"), {.value = "platform-preview"});
    REQUIRE(repeated.ok());
    CHECK(fabric::project::serialize_map_package_manifest(*repeated.manifest) ==
          fabric::project::serialize_map_package_manifest(*planned.manifest));
}

TEST_CASE("map package planning includes native vector dependencies") {
    const auto planned = fabric::project::plan_map_package(
        fixture("studio-textile-head"), {.value = "textile-head-preview"});
    REQUIRE(planned.ok());
    CHECK(std::ranges::count_if(
              planned.manifest->resources, [](const auto& resource) {
                  return resource.resource.expected_type == "vector";
              }) == 2);
}
