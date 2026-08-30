#include "fabric/project/map_package.hpp"
#include "fabric/project/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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

fabric::project::SceneDocument scene(const char* id, const char* target) {
    fabric::project::SceneDocument result{
        .document = {.schema_version = 1, .type = "scene",
                     .id = {.value = id}, .name = id},
        .maps = {{{{.value = "platform-preview"}, "map"}, "world"}},
        .entry_map = fabric::project::ResourceReference{
            {.value = "platform-preview"}, "map"}};
    result.transitions.push_back({"continue", {{.value = target}, "scene"},
                                  "start", std::nullopt});
    return result;
}

std::filesystem::path fixture(const std::string_view name) {
    return std::filesystem::path{__FILE__}.parent_path().parent_path() /
        "fixtures" / name;
}

class TemporaryProject {
public:
    explicit TemporaryProject(const std::string_view source) {
        static std::uint64_t sequence{};
        root_ = std::filesystem::temp_directory_path() /
            ("fabric-map-package-" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch().count()) +
             "-" + std::to_string(sequence++));
        outside_ = root_.string() + "-outside";
        std::filesystem::copy(fixture(source), root_,
                              std::filesystem::copy_options::recursive);
    }
    ~TemporaryProject() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::remove_all(outside_, ignored);
    }
    const std::filesystem::path& root() const { return root_; }
    const std::filesystem::path& outside() const { return outside_; }

private:
    std::filesystem::path root_;
    std::filesystem::path outside_;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void replace_in_file(const std::filesystem::path& path,
                     const std::string_view from,
                     const std::string_view to) {
    auto contents = read_file(path);
    const auto position = contents.find(from);
    if (position == std::string::npos)
        throw std::runtime_error("test replacement source is missing");
    contents.replace(position, from.size(), to);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
}

template <typename Result>
bool has_error(const Result& result, const fabric::project::ErrorCode code) {
    return std::ranges::any_of(result.errors, [&](const auto& error) {
        return error.code == code;
    });
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
        {"vector", "platform-strip-border"},
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
              }) == 5);
}

TEST_CASE("map package planning rejects missing and absolute resources") {
    TemporaryProject missing{"studio-rotating-platform"};
    std::filesystem::remove(
        missing.root() /
        "assets/components/platform-strip.component.json");
    auto planned = fabric::project::plan_map_package(
        missing.root(), {.value = "platform-preview"});
    CHECK_FALSE(planned.ok());
    CHECK(has_error(planned, fabric::project::ErrorCode::missing_file));

    TemporaryProject absolute{"studio-rotating-platform"};
    replace_in_file(
        absolute.root() /
            "assets/textures/platform-thread.texture.json",
        "assets/textures/platform-thread.png", "/outside/thread.png");
    planned = fabric::project::plan_map_package(
        absolute.root(), {.value = "platform-preview"});
    CHECK_FALSE(planned.ok());
    CHECK(has_error(planned, fabric::project::ErrorCode::invalid_path));
}

TEST_CASE("map package planning rejects an external payload symlink") {
    TemporaryProject project{"studio-rotating-platform"};
    std::filesystem::create_directories(project.outside());
    const auto external = project.outside() / "thread.png";
    {
        std::ofstream output(external, std::ios::binary);
        output << "external";
    }
    const auto payload = project.root() /
        "assets/textures/platform-thread.png";
    std::filesystem::remove(payload);
    std::error_code link_error;
    std::filesystem::create_symlink(external, payload, link_error);
    if (link_error) SKIP("symbolic links are unavailable");
    const auto planned = fabric::project::plan_map_package(
        project.root(), {.value = "platform-preview"});
    CHECK_FALSE(planned.ok());
    CHECK(has_error(planned, fabric::project::ErrorCode::invalid_path));
}

TEST_CASE("map package planning rejects dependency cycles") {
    TemporaryProject project{"studio-rotating-platform"};
    const auto composition = project.root() /
        "assets/compositions/platform-strip-composition.composition.json";
    replace_in_file(
        composition, "\"kind\": \"texturedPath\"",
        "\"componentInstance\": {\n        \"overrides\": []\n      },\n      \"kind\": \"component\"");
    replace_in_file(composition, "\"expectedType\": \"texturedPath\"",
                    "\"expectedType\": \"visualComponent\"");
    replace_in_file(composition, "\"id\": \"platform-strip-rail\"",
                    "\"id\": \"platform-strip\"");
    const auto planned = fabric::project::plan_map_package(
        project.root(), {.value = "platform-preview"});
    CHECK_FALSE(planned.ok());
    CHECK(has_error(planned, fabric::project::ErrorCode::resource_cycle));
}

TEST_CASE("map package planning rejects identifiers shared by two types") {
    TemporaryProject project{"studio-rotating-platform"};
    const auto mechanics = project.root() / "assets/mechanics";
    auto graph = read_file(mechanics / "rotating-platform.mechanic.json");
    const auto id = graph.find("\"id\": \"rotating-platform\"");
    REQUIRE(id != std::string::npos);
    graph.replace(id, std::string_view{"\"id\": \"rotating-platform\""}.size(),
                  "\"id\": \"platform-visual\"");
    {
        std::ofstream output(mechanics / "platform-visual.mechanic.json",
                             std::ios::binary);
        output << graph;
    }
    replace_in_file(project.root() / "maps/platform-preview.map.json",
                    "\"id\": \"rotating-platform\"",
                    "\"id\": \"platform-visual\"");
    const auto planned = fabric::project::plan_map_package(
        project.root(), {.value = "platform-preview"});
    CHECK_FALSE(planned.ok());
    CHECK(has_error(planned, fabric::project::ErrorCode::duplicate_resource));
}

TEST_CASE("map package publication copies the planned closure without overwrite") {
    TemporaryProject project{"studio-rotating-platform"};
    const auto destination = project.root().parent_path() /
        (project.root().filename().string() + "-published");
    std::error_code cleanup_error;
    std::filesystem::remove_all(destination, cleanup_error);
    const auto published = fabric::project::publish_map_package(
        project.root(), {.value = "platform-preview"}, destination);
    REQUIRE(published.ok());
    CHECK(std::filesystem::is_regular_file(
        destination / "map-package.json"));
    CHECK(std::filesystem::is_regular_file(
        destination / "maps/platform-preview.map.json"));
    CHECK(std::filesystem::is_regular_file(
        destination / "assets/textures/platform-thread.png"));

    const auto rejected = fabric::project::publish_map_package(
        project.root(), {.value = "platform-preview"}, destination);
    CHECK_FALSE(rejected.ok());
    CHECK(has_error(rejected,
                    fabric::project::ErrorCode::asset_already_exists));
    std::filesystem::remove_all(destination, cleanup_error);
}

TEST_CASE("scene package planning closes a cyclic scene campaign") {
    TemporaryProject project{"studio-rotating-platform"};
    const auto loaded = fabric::project::load_manifest(project.root());
    REQUIRE(loaded.ok());
    REQUIRE(fabric::project::publish_scene(
        project.root(), *loaded.manifest, scene("scene-a", "scene-b")).ok());
    REQUIRE(fabric::project::publish_scene(
        project.root(), *loaded.manifest, scene("scene-b", "scene-a")).ok());

    const auto planned = fabric::project::plan_scene_package(
        project.root(), {.value = "scene-a"});
    REQUIRE(planned.ok());
    CHECK(planned.manifest->root_scene == fabric::project::ResourceReference{
        {.value = "scene-a"}, "scene"});
    CHECK(std::ranges::count_if(
              planned.manifest->resources, [](const auto& resource) {
                  return resource.resource.expected_type == "scene";
              }) == 2);
    CHECK(std::ranges::count_if(
              planned.manifest->resources, [](const auto& resource) {
                  return resource.resource.expected_type == "map";
              }) == 1);
    CHECK(std::ranges::count_if(
              planned.manifest->resources, [](const auto& resource) {
                  return resource.resource.expected_type == "mechanic";
              }) == 1);

    const auto serialized =
        fabric::project::serialize_scene_package_manifest(*planned.manifest);
    const auto parsed =
        fabric::project::parse_scene_package_manifest(serialized);
    REQUIRE(parsed.ok());
    CHECK(*parsed.manifest == *planned.manifest);
    CHECK(fabric::project::runtime_can_load_scene_package(*parsed.manifest));
}

TEST_CASE("scene package publication copies every scene and map dependency") {
    TemporaryProject project{"studio-rotating-platform"};
    const auto loaded = fabric::project::load_manifest(project.root());
    REQUIRE(loaded.ok());
    REQUIRE(fabric::project::publish_scene(
        project.root(), *loaded.manifest, scene("scene-a", "scene-b")).ok());
    REQUIRE(fabric::project::publish_scene(
        project.root(), *loaded.manifest, scene("scene-b", "scene-a")).ok());
    const auto destination = project.root().parent_path() /
        (project.root().filename().string() + "-scene-published");
    std::error_code cleanup_error;
    std::filesystem::remove_all(destination, cleanup_error);

    const auto published = fabric::project::publish_scene_package(
        project.root(), {.value = "scene-a"}, destination);
    REQUIRE(published.ok());
    CHECK(std::filesystem::is_regular_file(
        destination / "scene-package.json"));
    CHECK(std::filesystem::is_regular_file(
        destination / "scenes/scene-a.scene.json"));
    CHECK(std::filesystem::is_regular_file(
        destination / "scenes/scene-b.scene.json"));
    CHECK(std::filesystem::is_regular_file(
        destination / "maps/platform-preview.map.json"));
    CHECK(std::filesystem::is_regular_file(
        destination / "assets/textures/platform-thread.png"));

    const auto rejected = fabric::project::publish_scene_package(
        project.root(), {.value = "scene-a"}, destination);
    CHECK_FALSE(rejected.ok());
    CHECK(has_error(rejected,
                    fabric::project::ErrorCode::asset_already_exists));
    std::filesystem::remove_all(destination, cleanup_error);
}
