#include "fabric/editor/creation_prompts.hpp"
#include "fabric/project/texture_asset.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("fabric-prompt-test-" + std::to_string(unique));
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

fabric::project::ProjectManifest manifest() {
    return {
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "prompt-tests"},
        .name = "Prompt tests",
        .pixels_per_unit = 100.0,
        .directories = {},
    };
}

void write_texture_resource(const std::filesystem::path& project,
                            const std::string& id) {
    std::filesystem::create_directories(project / "assets/textures");
    const fabric::project::TextureAsset texture{
        .document = {
            .type = "texture",
            .id = {.value = id},
            .name = "Fill texture",
        },
        .source = "assets/textures/" + id + ".png",
        .width = 1,
        .height = 1,
    };
    std::ofstream{project / texture.source, std::ios::binary} << "source";
    std::ofstream{project / "assets/textures" / (id + ".texture.json"),
                  std::ios::binary}
        << fabric::project::serialize_texture_asset(texture);
}

} // namespace

TEST_CASE("project creation prompt exposes typed defaults and exact output") {
    TemporaryDirectory parent;
    fabric::editor::CreateProjectPrompt prompt;
    prompt.parent_directory = parent.path();
    prompt.name = "Needlework";

    const auto validation = prompt.validate();
    REQUIRE(validation.ok());
    CHECK(prompt.project_root() == parent.path() / "needlework");
    CHECK(validation.destination == prompt.project_root() / "project.json");
    CHECK(prompt.manifest().pixels_per_unit == 100.0);
    CHECK(prompt.manifest().id.value == "needlework");
    CHECK(validation.summary.back().find(
              validation.destination.generic_string()) != std::string::npos);
}

TEST_CASE("visible names produce stable internal identifiers") {
    CHECK(fabric::editor::generated_resource_id("  Épée d'Œuvre  ").value ==
          "epee-d-oeuvre");
    CHECK(fabric::editor::generated_resource_id("日本語", "artwork").value ==
          "artwork");
}

TEST_CASE("project prompt reports field errors without publishing") {
    TemporaryDirectory occupied;
    std::filesystem::create_directory(occupied.path() / "invalid");
    std::ofstream{occupied.path() / "invalid/keep.txt"} << "keep";
    fabric::editor::CreateProjectPrompt prompt;
    prompt.parent_directory = occupied.path();
    prompt.name = "Invalid";
    prompt.pixels_per_unit = 0.0;

    const auto validation = prompt.validate();
    CHECK_FALSE(validation.ok());
    CHECK(validation.error_for("destination").has_value());
    CHECK_FALSE(validation.error_for("name").has_value());
    CHECK(validation.error_for("pixelsPerUnit").has_value());
    CHECK_FALSE(std::filesystem::exists(occupied.path() / "project.json"));
}

TEST_CASE("project parent may contain unrelated files") {
    TemporaryDirectory parent;
    std::ofstream{parent.path() / "keep.txt"} << "keep";
    fabric::editor::CreateProjectPrompt prompt;
    prompt.parent_directory = parent.path();
    prompt.name = "New project";

    const auto validation = prompt.validate();
    REQUIRE(validation.ok());
    CHECK(prompt.project_root() == parent.path() / "new-project");
    CHECK(validation.destination ==
          parent.path() / "new-project/project.json");
}

TEST_CASE("selecting a project preset updates scale without hidden state") {
    fabric::editor::CreateProjectPrompt prompt;
    prompt.select_preset(fabric::editor::ProjectScalePreset::high_detail);
    CHECK(prompt.preset == fabric::editor::ProjectScalePreset::high_detail);
    CHECK(prompt.pixels_per_unit == 256.0);

    prompt.select_preset(fabric::editor::ProjectScalePreset::custom);
    CHECK(prompt.pixels_per_unit == 256.0);
}

TEST_CASE("source import prompts validate type destination and conflict before import") {
    TemporaryDirectory project;
    std::filesystem::create_directories(project.path() / "assets/textures");
    const auto source = project.path() / "fill.png";
    std::ofstream{source} << "preview validation only";
    fabric::editor::ImportSourcePrompt prompt{
        .source = source,
        .name = "Fabric fill",
    };

    const auto valid = prompt.validate(
        fabric::editor::ImportSourceKind::png_image, project.path(),
        manifest());
    REQUIRE(valid.ok());
    CHECK(valid.destination ==
          project.path() / "assets/textures/fabric-fill.texture.json");

    std::ofstream{project.path() /
                  "assets/textures/fabric-fill.texture.json"}
        << "{}";
    const auto conflict = prompt.validate(
        fabric::editor::ImportSourceKind::png_image, project.path(),
        manifest());
    REQUIRE(conflict.ok());
    CHECK(prompt.resource_id(project.path(), manifest()).value ==
          "fabric-fill-2");
    CHECK(conflict.destination ==
          project.path() / "assets/textures/fabric-fill-2.texture.json");
}

TEST_CASE("different import prompts never share or retain cancelled state") {
    fabric::editor::ImportSourcePrompt png;
    fabric::editor::ImportSourcePrompt svg;
    png.name = "PNG state";
    svg.name = "SVG state";

    png.reset();
    CHECK(png.name.empty());
    CHECK(svg.name == "SVG state");
    CHECK(fabric::editor::generated_resource_id(svg.name).value == "svg-state");
}

TEST_CASE("vector artwork prompt validates dimensions and resource conflicts") {
    TemporaryDirectory project;
    std::filesystem::create_directories(project.path() / "assets/vectors");
    std::ofstream{project.path() / "assets/vectors/occupied.vector.json"}
        << "{}";
    fabric::editor::CreateVectorArtworkPrompt prompt;
    prompt.name = "Occupied";
    prompt.width = -1.0;
    prompt.initial_color.alpha = 2.0F;

    const auto validation = prompt.validate(project.path(), manifest());
    CHECK_FALSE(validation.ok());
    CHECK(validation.error_for("width").has_value());
    CHECK(validation.error_for("initialFill").has_value());
    CHECK(validation.destination ==
          project.path() / "assets/vectors/occupied-2.vector.json");
}

TEST_CASE("project and artwork prompt states are isolated and cancellable") {
    fabric::editor::CreateProjectPrompt project_prompt;
    fabric::editor::CreateVectorArtworkPrompt artwork_prompt;
    project_prompt.name = "Project state";
    artwork_prompt.name = "Artwork state";

    project_prompt.reset();
    CHECK(project_prompt.name.empty());
    CHECK(artwork_prompt.name == "Artwork state");
    CHECK(fabric::editor::generated_resource_id(artwork_prompt.name).value ==
          "artwork-state");

    artwork_prompt.reset();
    CHECK(artwork_prompt.name.empty());
    CHECK(artwork_prompt.width == 10.0);
    CHECK(project_prompt.pixels_per_unit == 100.0);
}

TEST_CASE("vector artwork image fill validates resource and adjustable mapping") {
    TemporaryDirectory project;
    write_texture_resource(project.path(), "woven-photo");
    fabric::editor::CreateVectorArtworkPrompt prompt;
    prompt.name = "Image panel";
    prompt.initial_fill = fabric::editor::InitialFill::image;
    prompt.initial_image_id = "woven-photo";
    prompt.image_fit = fabric::project::VectorImageFit::cover;
    prompt.image_transform.position = {0.25F, -0.1F};
    prompt.image_transform.scale = {1.5F, 0.75F};
    prompt.image_opacity = 0.8;
    prompt.deform_image_with_shape = true;

    const auto valid = prompt.validate(project.path(), manifest());
    REQUIRE(valid.ok());
    CHECK(valid.summary.end() != std::find_if(
              valid.summary.begin(), valid.summary.end(),
              [](const std::string& line) {
                  return line.find("Deforms with shape: yes") !=
                         std::string::npos;
              }));

    prompt.initial_image_id = "missing";
    CHECK(prompt.validate(project.path(), manifest())
              .error_for("initialImage")
              .has_value());
}
