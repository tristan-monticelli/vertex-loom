#include "fabric/core/resource_id.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/texture_asset.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

using fabric::project::ErrorCode;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryProject {
public:
    TemporaryProject() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        root_ = std::filesystem::temp_directory_path() /
                ("fabric-project-test-" + std::to_string(unique));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryProject() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    TemporaryProject(const TemporaryProject&) = delete;
    TemporaryProject& operator=(const TemporaryProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

private:
    std::filesystem::path root_;
};

bool contains_error(const std::vector<fabric::project::Error>& errors,
                    const ErrorCode code, const std::string_view field) {
    for (const auto& error : errors) {
        if (error.code == code && error.field == field) {
            return true;
        }
    }
    return false;
}

void resource_ids_are_strict() {
    using fabric::core::ResourceId;
    require(ResourceId::is_valid("hero.wool-01"), "valid resource ID rejected");
    require(!ResourceId::is_valid(""), "empty resource ID accepted");
    require(!ResourceId::is_valid("Hero"), "uppercase resource ID accepted");
    require(!ResourceId::is_valid("-hero"), "punctuation prefix accepted");
    require(!ResourceId::is_valid("hero/coat"), "path-like resource ID accepted");
}

fabric::project::ProjectManifest example_manifest() {
    return {
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "textile-demo"},
        .name = "Textile Demo",
        .directories = {},
    };
}

void manifest_round_trip_is_lossless() {
    const auto expected = example_manifest();
    const auto parsed = fabric::project::parse_manifest(
        fabric::project::serialize_manifest(expected));
    require(parsed.ok(), "serialized manifest did not parse");
    require(*parsed.manifest == expected, "manifest round-trip lost data");
}

void legacy_manifest_is_migrated() {
    const auto migrated = fabric::project::parse_manifest(R"({
        "schemaVersion": 0,
        "projectId": "legacy-project",
        "displayName": "Legacy Project",
        "assetsPath": "content"
    })");
    require(migrated.ok(), "legacy manifest did not migrate");
    require(migrated.manifest->schema_version == 1,
            "legacy manifest has the wrong target version");
    require(migrated.manifest->id.value == "legacy-project",
            "legacy project ID was not migrated");
    require(migrated.manifest->name == "Legacy Project",
            "legacy display name was not migrated");
    require(migrated.manifest->directories.assets == "content",
            "legacy assets path was not migrated");
    require(migrated.manifest->directories.entities == "entities",
            "migration did not add current directory defaults");
}

void invalid_contracts_are_rejected() {
    const auto unsupported = fabric::project::parse_manifest(R"({
        "schemaVersion": 99,
        "id": "textile-demo",
        "name": "Textile Demo",
        "directories": {
            "assets": "assets",
            "entities": "entities",
            "maps": "maps",
            "scenes": "scenes",
            "schemas": "schemas"
        }
    })");
    require(!unsupported.ok(), "unsupported schema version accepted");
    require(contains_error(unsupported.errors,
                           ErrorCode::unsupported_schema_version,
                           "schemaVersion"),
            "unsupported schema version did not produce a structured error");

    auto traversal = example_manifest();
    traversal.directories.assets = "../outside";
    const auto report = fabric::project::validate_manifest(traversal);
    require(!report.ok(), "traversing path accepted");
    require(contains_error(report.errors, ErrorCode::invalid_path,
                           "directories.assets"),
            "traversing path did not produce a structured error");

    traversal.directories.assets = "C:\\outside";
    const auto windows_absolute = fabric::project::validate_manifest(traversal);
    require(contains_error(windows_absolute.errors, ErrorCode::invalid_path,
                           "directories.assets"),
            "Windows absolute path accepted");
}

void complete_project_directory_is_accepted() {
    const TemporaryProject project;
    for (const auto* directory : {"assets", "entities", "maps", "scenes", "schemas"}) {
        std::filesystem::create_directory(project.root() / directory);
    }
    {
        std::ofstream output(project.root() / "project.json", std::ios::binary);
        output << fabric::project::serialize_manifest(example_manifest());
    }

    const auto loaded = fabric::project::load_project(project.root());
    require(loaded.ok(), "complete project directory rejected");
    require(*loaded.manifest == example_manifest(),
            "validated project returned the wrong manifest");
}

void project_creation_builds_a_loadable_structure() {
    const TemporaryProject parent;
    const auto project_root = parent.root() / "created-project";
    const auto expected = example_manifest();

    const auto created = fabric::project::create_project(project_root, expected);
    require(created.ok(), "project creation failed");
    require(*created.manifest == expected,
            "project creation returned the wrong manifest");
    require(std::filesystem::is_regular_file(project_root / "project.json"),
            "project creation did not write project.json");
    for (const auto* directory : {"assets", "entities", "maps", "scenes", "schemas"}) {
        require(std::filesystem::is_directory(project_root / directory),
                "project creation omitted a required directory");
    }
    for (const auto* directory : {"textures", "vectors", "animations", "materials"}) {
        require(std::filesystem::is_directory(
                    project_root / "assets" / directory),
                "project creation omitted a standard asset directory");
    }
}

void project_creation_never_overwrites_a_nonempty_destination() {
    const TemporaryProject parent;
    const auto project_root = parent.root() / "occupied";
    std::filesystem::create_directory(project_root);
    {
        std::ofstream sentinel(project_root / "keep.txt", std::ios::binary);
        sentinel << "keep";
    }

    const auto rejected = fabric::project::create_project(
        project_root, example_manifest());
    require(!rejected.ok(), "nonempty destination was accepted");
    require(contains_error(rejected.errors, ErrorCode::directory_not_empty,
                           "project"),
            "nonempty destination produced the wrong error");
    require(std::filesystem::is_regular_file(project_root / "keep.txt"),
            "project creation removed an existing file");
    require(!std::filesystem::exists(project_root / "project.json"),
            "project creation overwrote the occupied destination");
}

void invalid_project_creation_writes_nothing() {
    const TemporaryProject parent;
    const auto project_root = parent.root() / "invalid-project";
    auto invalid = example_manifest();
    invalid.id.value = "Invalid ID";

    const auto rejected = fabric::project::create_project(project_root, invalid);
    require(!rejected.ok(), "invalid project was created");
    require(!std::filesystem::exists(project_root),
            "invalid manifest created a destination directory");
}

void atomic_save_preserves_the_previous_valid_manifest() {
    const TemporaryProject project;
    const auto original = example_manifest();
    require(fabric::project::save_manifest_atomic(project.root(), original).ok(),
            "initial atomic save failed");

    auto invalid = original;
    invalid.id.value = "Invalid ID";
    const auto rejected = fabric::project::save_manifest_atomic(project.root(), invalid);
    require(!rejected.ok(), "invalid manifest was saved");

    const auto loaded = fabric::project::load_manifest(project.root());
    require(loaded.ok(), "previous manifest became unreadable");
    require(*loaded.manifest == original,
            "failed save replaced the previous manifest");
}

void texture_asset_round_trip_is_lossless() {
    const auto manifest = example_manifest();
    const fabric::project::TextureAsset expected{
        .document = {
            .schema_version = fabric::project::current_texture_schema_version,
            .type = "texture",
            .id = {.value = "wool-fill"},
            .name = "Wool Fill",
        },
        .source = "assets/textures/wool-fill.png",
        .width = 32,
        .height = 24,
        .pixel_format = "rgba8",
    };
    const auto parsed = fabric::project::parse_texture_asset(
        manifest, fabric::project::serialize_texture_asset(expected));
    require(parsed.ok(), "serialized texture asset did not parse");
    require(*parsed.asset == expected, "texture round-trip lost data");
}

void invalid_texture_paths_are_rejected() {
    auto asset = fabric::project::TextureAsset{
        .document = {.id = {.value = "wool-fill"}, .name = "Wool Fill"},
        .source = "../outside.png",
        .width = 1,
        .height = 1,
    };
    const auto report = fabric::project::validate_texture_asset(
        example_manifest(), asset);
    require(contains_error(report.errors, ErrorCode::invalid_path, "source"),
            "traversing texture source was accepted");
}

void project_validation_rejects_a_missing_texture_source() {
    const TemporaryProject project;
    for (const auto* directory : {"assets/textures", "entities", "maps", "scenes", "schemas"}) {
        std::filesystem::create_directories(project.root() / directory);
    }
    const auto manifest = example_manifest();
    {
        std::ofstream output(project.root() / "project.json", std::ios::binary);
        output << fabric::project::serialize_manifest(manifest);
    }
    const fabric::project::TextureAsset texture{
        .document = {.id = {.value = "missing"}, .name = "Missing"},
        .source = "assets/textures/missing.png",
        .width = 1,
        .height = 1,
    };
    {
        std::ofstream output(
            project.root() / "assets/textures/missing.texture.json",
            std::ios::binary);
        output << fabric::project::serialize_texture_asset(texture);
    }

    const auto report = fabric::project::validate_project(project.root());
    require(contains_error(report.errors, ErrorCode::missing_file, "source"),
            "project validator accepted a missing texture source");
}

} // namespace

int main() {
    resource_ids_are_strict();
    manifest_round_trip_is_lossless();
    legacy_manifest_is_migrated();
    invalid_contracts_are_rejected();
    complete_project_directory_is_accepted();
    project_creation_builds_a_loadable_structure();
    project_creation_never_overwrites_a_nonempty_destination();
    invalid_project_creation_writes_nothing();
    atomic_save_preserves_the_previous_valid_manifest();
    texture_asset_round_trip_is_lossless();
    invalid_texture_paths_are_rejected();
    project_validation_rejects_a_missing_texture_source();
    return 0;
}
