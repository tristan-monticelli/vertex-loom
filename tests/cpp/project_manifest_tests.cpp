#include "fabric/core/resource_id.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    require(migrated.manifest->schema_version == 2,
            "legacy manifest has the wrong target version");
    require(migrated.manifest->id.value == "legacy-project",
            "legacy project ID was not migrated");
    require(migrated.manifest->name == "Legacy Project",
            "legacy display name was not migrated");
    require(migrated.manifest->directories.assets == "content",
            "legacy assets path was not migrated");
    require(migrated.manifest->directories.entities == "entities",
            "migration did not add current directory defaults");
    require(migrated.manifest->pixels_per_unit == 100.0,
            "migration did not add the default unit scale");
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
        .document = {
            .type = "texture",
            .id = {.value = "wool-fill"},
            .name = "Wool Fill",
        },
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
        .document = {
            .type = "texture",
            .id = {.value = "missing"},
            .name = "Missing",
        },
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

void vector_asset_round_trip_is_lossless() {
    const auto manifest = example_manifest();
    const fabric::project::VectorAsset expected{
        .document = {
            .schema_version = fabric::project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = "thread-outline"},
            .name = "Thread Outline",
        },
        .source_kind = fabric::project::VectorSourceKind::linked_svg,
        .source = "assets/vectors/thread-outline.svg",
    };
    const auto parsed = fabric::project::parse_vector_asset(
        manifest, fabric::project::serialize_vector_asset(expected));
    require(parsed.ok(), "serialized vector asset did not parse");
    require(*parsed.asset == expected, "vector round-trip lost data");
}

void legacy_vector_asset_migrates_without_changing_its_source() {
    constexpr std::string_view legacy = R"({
  "schemaVersion": 1,
  "type": "vector",
  "id": "thread-outline",
  "name": "Thread Outline",
  "source": "assets/vectors/thread-outline.svg",
  "format": "svg"
})";
    const auto migrated = fabric::project::parse_vector_asset(
        example_manifest(), legacy);
    require(migrated.ok(), "legacy vector asset did not migrate");
    require(migrated.asset->document.schema_version ==
                fabric::project::current_vector_schema_version,
            "legacy vector retained the old schema version");
    require(migrated.asset->source_kind ==
                fabric::project::VectorSourceKind::linked_svg,
            "legacy SVG did not become linkedSvg");
    require(migrated.asset->source ==
                "assets/vectors/thread-outline.svg",
            "legacy SVG source path changed during migration");
    const auto serialized =
        fabric::project::serialize_vector_asset(*migrated.asset);
    require(serialized.find("\"sourceKind\": \"linkedSvg\"") !=
                std::string::npos,
            "migrated vector did not serialize as linkedSvg");
    require(serialized.find("\"format\"") == std::string::npos,
            "migrated vector retained the obsolete format field");
}

void loading_a_legacy_vector_never_rewrites_the_svg() {
    const TemporaryProject project;
    std::filesystem::create_directories(project.root() / "assets/vectors");
    const auto manifest = example_manifest();
    constexpr std::string_view source_bytes =
        "<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M0 0L1 1\"/></svg>";
    {
        std::ofstream source(
            project.root() / "assets/vectors/thread-outline.svg",
            std::ios::binary);
        source << source_bytes;
        std::ofstream document(
            project.root() / "assets/vectors/thread-outline.vector.json",
            std::ios::binary);
        document << R"({
  "schemaVersion": 1,
  "type": "vector",
  "id": "thread-outline",
  "name": "Thread Outline",
  "source": "assets/vectors/thread-outline.svg",
  "format": "svg"
})";
    }

    const auto loaded = fabric::project::load_vector_asset(
        project.root(), manifest,
        "assets/vectors/thread-outline.vector.json");
    require(loaded.ok(), "legacy vector document did not load through migration");
    std::ifstream source(
        project.root() / "assets/vectors/thread-outline.svg",
        std::ios::binary);
    const std::string after{std::istreambuf_iterator<char>{source},
                            std::istreambuf_iterator<char>{}};
    require(after == source_bytes,
            "loading the legacy vector rewrote its SVG source bytes");
}

void native_vector_without_geometry_is_rejected() {
    constexpr std::string_view native_without_geometry = R"({
  "schemaVersion": 2,
  "type": "vector",
  "id": "thread-outline",
  "name": "Thread Outline",
  "sourceKind": "native"
})";
    const auto parsed = fabric::project::parse_vector_asset(
        example_manifest(), native_without_geometry);
    require(!parsed.ok(),
            "native vector without geometry was accepted as a false success");
    require(!parsed.errors.empty() && parsed.errors.front().field == "native",
            "native vector rejection did not identify unavailable geometry");
}

fabric::project::VectorAsset native_rectangle_asset() {
    return {
        .document = {
            .schema_version = fabric::project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = "native-panel"},
            .name = "Native Panel",
        },
        .source_kind = fabric::project::VectorSourceKind::native,
        .native = fabric::project::NativeVectorDefinition{
            .size = {12.0F, 8.0F},
            .origin = fabric::project::VectorOrigin::center,
            .nodes = {{
                .id = "node-1",
                .name = "Panel",
                .shape = {
                    .id = "shape-1",
                    .kind = fabric::project::VectorShapeKind::rectangle,
                    .bounds = {.origin = {-6.0F, -4.0F},
                               .size = {12.0F, 8.0F}},
                },
                .fill = {
                    .kind = fabric::project::VectorFillKind::solid,
                    .color = fabric::core::Color{
                        0.2F, 0.4F, 0.6F, 0.8F},
                },
            }},
        },
    };
}

void native_vector_round_trips_and_publishes_without_svg() {
    const auto manifest = example_manifest();
    const auto expected = native_rectangle_asset();
    const auto parsed = fabric::project::parse_vector_asset(
        manifest, fabric::project::serialize_vector_asset(expected));
    require(parsed.ok(), "native vector did not round-trip");
    require(*parsed.asset == expected, "native vector round-trip lost data");

    const TemporaryProject project;
    const auto published = fabric::project::publish_native_vector_asset(
        project.root(), manifest, expected);
    require(published.ok(), "native vector was not published atomically");
    require(*published.asset == expected,
            "published native vector did not reload identically");
    require(std::filesystem::is_regular_file(
                project.root() /
                "assets/vectors/native-panel.vector.json"),
            "native vector document is missing");
    require(!std::filesystem::exists(
                project.root() / "assets/vectors/native-panel.svg"),
            "native vector unexpectedly created an SVG source");
}

void native_vector_rejects_duplicate_stable_identifiers() {
    auto invalid = native_rectangle_asset();
    invalid.native->nodes.push_back(invalid.native->nodes.front());
    const auto validation = fabric::project::validate_vector_asset(
        example_manifest(), invalid);
    require(!validation.ok(), "duplicate native identifiers were accepted");
}

void vector_source_kinds_reject_ambiguous_payloads() {
    const auto manifest = example_manifest();
    const auto native_with_source = fabric::project::parse_vector_asset(
        manifest, R"({
  "schemaVersion": 2,
  "type": "vector",
  "id": "ambiguous-native",
  "name": "Ambiguous Native",
  "sourceKind": "native",
  "source": "assets/vectors/ambiguous-native.svg",
  "native": {"size": {"x": 1, "y": 1}, "origin": "center", "nodes": []}
})");
    require(!native_with_source.ok(),
            "native vector silently accepted an SVG source");

    const auto linked_with_native = fabric::project::parse_vector_asset(
        manifest, R"({
  "schemaVersion": 2,
  "type": "vector",
  "id": "ambiguous-linked",
  "name": "Ambiguous Linked",
  "sourceKind": "linkedSvg",
  "source": "assets/vectors/ambiguous-linked.svg",
  "native": {"size": {"x": 1, "y": 1}, "origin": "center", "nodes": []}
})");
    require(!linked_with_native.ok(),
            "linked SVG silently accepted native geometry");
}

void image_fill_round_trips_with_adjustable_shape_mapping() {
    auto expected = native_rectangle_asset();
    auto& fill = expected.native->nodes.front().fill;
    fill.kind = fabric::project::VectorFillKind::image;
    fill.color.reset();
    fill.image = fabric::project::VectorImageFill{
        .texture = {{.value = "fabric-photo"}, "texture"},
        .fit = fabric::project::VectorImageFit::cover,
        .transform = {
            .position = {0.2F, -0.1F},
            .rotation_degrees = 12.0F,
            .scale = {1.4F, 0.8F},
            .pivot = {0.5F, 0.5F},
        },
        .opacity = 0.75F,
        .deform_with_shape = true,
    };

    const auto parsed = fabric::project::parse_vector_asset(
        example_manifest(),
        fabric::project::serialize_vector_asset(expected));
    require(parsed.ok(), "image fill vector did not round-trip");
    require(*parsed.asset == expected,
            "image fill mapping changed during round-trip");
    const auto references =
        fabric::project::vector_resource_references(*parsed.asset);
    require(references.size() == 1 &&
                references.front().id.value == "fabric-photo" &&
                references.front().expected_type == "texture",
            "image fill texture was not exposed to the resource graph");
}

void line_shape_round_trips_and_rejects_invalid_endpoints() {
    auto expected = native_rectangle_asset();
    auto& shape = expected.native->nodes.front().shape;
    shape.kind = fabric::project::VectorShapeKind::line;
    shape.bounds = {.origin = {-4.0F, -2.0F}, .size = {8.0F, 4.0F}};
    shape.points = {{-4.0F, -2.0F}, {4.0F, 2.0F}};

    const auto parsed = fabric::project::parse_vector_asset(
        example_manifest(), fabric::project::serialize_vector_asset(expected));
    require(parsed.ok(), "line vector did not round-trip");
    require(*parsed.asset == expected, "line points changed during round-trip");

    auto invalid = expected;
    invalid.native->nodes.front().shape.points = {{0.0F, 0.0F}, {0.0F, 0.0F}};
    const auto validation = fabric::project::validate_vector_asset(
        example_manifest(), invalid);
    require(!validation.ok(), "coincident line endpoints were accepted");
}

void path_shape_round_trips_and_rejects_invalid_sequences() {
    auto expected = native_rectangle_asset();
    auto& shape = expected.native->nodes.front().shape;
    shape.kind = fabric::project::VectorShapeKind::path;
    shape.bounds = {.origin = {-4.0F, -3.0F}, .size = {8.0F, 6.0F}};
    shape.path = {
        {.kind = fabric::project::VectorPathCommandKind::move,
         .point = {-4.0F, -3.0F}},
        {.kind = fabric::project::VectorPathCommandKind::cubic,
         .point = {4.0F, -3.0F},
         .control1 = {-1.0F, -6.0F},
         .control2 = {1.0F, 0.0F}},
        {.kind = fabric::project::VectorPathCommandKind::line,
         .point = {4.0F, 3.0F}},
        {.kind = fabric::project::VectorPathCommandKind::close},
    };

    const auto parsed = fabric::project::parse_vector_asset(
        example_manifest(), fabric::project::serialize_vector_asset(expected));
    require(parsed.ok(), "path vector did not round-trip");
    require(*parsed.asset == expected, "path commands changed during round-trip");

    auto invalid = expected;
    std::swap(invalid.native->nodes.front().shape.path[0],
              invalid.native->nodes.front().shape.path[1]);
    const auto validation = fabric::project::validate_vector_asset(
        example_manifest(), invalid);
    require(!validation.ok(), "path without an initial move was accepted");
}

void path_shape_rejects_self_intersections() {
    auto invalid = native_rectangle_asset();
    auto& shape = invalid.native->nodes.front().shape;
    shape.kind = fabric::project::VectorShapeKind::path;
    shape.bounds = {.origin = {0.0F, 0.0F}, .size = {10.0F, 10.0F}};
    shape.path = {
        {.kind = fabric::project::VectorPathCommandKind::move,
         .point = {0.0F, 0.0F}},
        {.kind = fabric::project::VectorPathCommandKind::line,
         .point = {10.0F, 10.0F}},
        {.kind = fabric::project::VectorPathCommandKind::line,
         .point = {0.0F, 10.0F}},
        {.kind = fabric::project::VectorPathCommandKind::line,
         .point = {10.0F, 0.0F}},
        {.kind = fabric::project::VectorPathCommandKind::close},
    };
    const auto validation = fabric::project::validate_vector_asset(
        example_manifest(), invalid);
    require(!validation.ok(), "self-intersecting path was accepted");
}

void stroke_round_trips_and_rejects_invalid_width() {
    auto expected = native_rectangle_asset();
    expected.native->nodes.front().stroke = fabric::project::VectorStroke{
        .color = {0.8F, 0.2F, 0.1F, 1.0F},
        .width = 2.5F,
        .join = fabric::project::VectorStrokeJoin::round,
        .cap = fabric::project::VectorStrokeCap::square,
    };
    const auto parsed = fabric::project::parse_vector_asset(
        example_manifest(), fabric::project::serialize_vector_asset(expected));
    require(parsed.ok(), "stroke vector did not round-trip");
    require(*parsed.asset == expected, "stroke changed during round-trip");

    auto invalid = expected;
    invalid.native->nodes.front().stroke->width = 0.0F;
    const auto validation = fabric::project::validate_vector_asset(
        example_manifest(), invalid);
    require(!validation.ok(), "zero-width stroke was accepted");
}

void node_hierarchy_and_clip_round_trip_and_reject_cycles() {
    auto expected = native_rectangle_asset();
    auto child = expected.native->nodes.front();
    child.id = "node-2";
    child.shape.id = "shape-2";
    child.parent_id = "node-1";
    child.clip_node_id = "node-1";
    expected.native->nodes.push_back(child);

    const auto parsed = fabric::project::parse_vector_asset(
        example_manifest(), fabric::project::serialize_vector_asset(expected));
    require(parsed.ok(), "node hierarchy did not round-trip");
    require(*parsed.asset == expected, "node hierarchy changed during round-trip");

    auto invalid = expected;
    invalid.native->nodes[0].parent_id = "node-2";
    const auto validation = fabric::project::validate_vector_asset(
        example_manifest(), invalid);
    require(!validation.ok(), "parent cycle was accepted");
}

void invalid_vector_paths_are_rejected() {
    auto asset = fabric::project::VectorAsset{
        .document = {
            .schema_version = fabric::project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = "thread-outline"},
            .name = "Thread Outline",
        },
        .source = "../outside.svg",
    };
    const auto report = fabric::project::validate_vector_asset(
        example_manifest(), asset);
    require(contains_error(report.errors, ErrorCode::invalid_path, "source"),
            "traversing vector source was accepted");
}

void project_validation_rejects_a_missing_vector_source() {
    const TemporaryProject project;
    for (const auto* directory :
         {"assets/vectors", "entities", "maps", "scenes", "schemas"}) {
        std::filesystem::create_directories(project.root() / directory);
    }
    const auto manifest = example_manifest();
    {
        std::ofstream output(project.root() / "project.json", std::ios::binary);
        output << fabric::project::serialize_manifest(manifest);
    }
    const fabric::project::VectorAsset vector{
        .document = {
            .schema_version = fabric::project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = "missing"},
            .name = "Missing",
        },
        .source = "assets/vectors/missing.svg",
    };
    {
        std::ofstream output(
            project.root() / "assets/vectors/missing.vector.json",
            std::ios::binary);
        output << fabric::project::serialize_vector_asset(vector);
    }

    const auto report = fabric::project::validate_project(project.root());
    require(contains_error(report.errors, ErrorCode::missing_file, "source"),
            "project validator accepted a missing vector source");
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
    vector_asset_round_trip_is_lossless();
    legacy_vector_asset_migrates_without_changing_its_source();
    loading_a_legacy_vector_never_rewrites_the_svg();
    native_vector_without_geometry_is_rejected();
    native_vector_round_trips_and_publishes_without_svg();
    native_vector_rejects_duplicate_stable_identifiers();
    vector_source_kinds_reject_ambiguous_payloads();
    image_fill_round_trips_with_adjustable_shape_mapping();
    line_shape_round_trips_and_rejects_invalid_endpoints();
    path_shape_round_trips_and_rejects_invalid_sequences();
    path_shape_rejects_self_intersections();
    stroke_round_trips_and_rejects_invalid_width();
    node_hierarchy_and_clip_round_trip_and_reject_cycles();
    invalid_vector_paths_are_rejected();
    project_validation_rejects_a_missing_vector_source();
    return 0;
}
