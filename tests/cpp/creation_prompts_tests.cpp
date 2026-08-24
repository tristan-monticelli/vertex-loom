#include "fabric/editor/creation_prompts.hpp"

#include <catch2/catch_test_macros.hpp>

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

} // namespace

TEST_CASE("project creation prompt exposes typed defaults and exact output") {
    TemporaryDirectory parent;
    fabric::editor::CreateProjectPrompt prompt;
    prompt.destination = parent.path() / "new-project";
    prompt.name = "Needlework";
    prompt.id = "needlework";

    const auto validation = prompt.validate();
    REQUIRE(validation.ok());
    CHECK(validation.destination == prompt.destination / "project.json");
    CHECK(prompt.manifest().pixels_per_unit == 100.0);
    CHECK(prompt.manifest().id.value == "needlework");
    CHECK(validation.summary.back().find(
              validation.destination.generic_string()) != std::string::npos);
}

TEST_CASE("project prompt reports field errors without publishing") {
    TemporaryDirectory occupied;
    std::ofstream{occupied.path() / "keep.txt"} << "keep";
    fabric::editor::CreateProjectPrompt prompt;
    prompt.destination = occupied.path();
    prompt.name = "   ";
    prompt.id = "Invalid ID";
    prompt.pixels_per_unit = 0.0;

    const auto validation = prompt.validate();
    CHECK_FALSE(validation.ok());
    CHECK(validation.error_for("destination").has_value());
    CHECK(validation.error_for("name").has_value());
    CHECK(validation.error_for("id").has_value());
    CHECK(validation.error_for("pixelsPerUnit").has_value());
    CHECK_FALSE(std::filesystem::exists(occupied.path() / "project.json"));
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
        .id = "fabric-fill",
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
    CHECK(conflict.error_for("id").has_value());
}

TEST_CASE("different import prompts never share or retain cancelled state") {
    fabric::editor::ImportSourcePrompt png;
    fabric::editor::ImportSourcePrompt svg;
    png.name = "PNG state";
    png.id = "png-state";
    svg.name = "SVG state";
    svg.id = "svg-state";

    png.reset();
    CHECK(png.name.empty());
    CHECK(png.id.empty());
    CHECK(svg.name == "SVG state");
    CHECK(svg.id == "svg-state");
}

TEST_CASE("vector artwork prompt validates dimensions and resource conflicts") {
    TemporaryDirectory project;
    std::filesystem::create_directories(project.path() / "assets/vectors");
    std::ofstream{project.path() / "assets/vectors/occupied.vector.json"}
        << "{}";
    fabric::editor::CreateVectorArtworkPrompt prompt;
    prompt.name = "Panel";
    prompt.id = "occupied";
    prompt.width = -1.0;
    prompt.initial_color.alpha = 2.0F;

    const auto validation = prompt.validate(project.path(), manifest());
    CHECK_FALSE(validation.ok());
    CHECK(validation.error_for("id").has_value());
    CHECK(validation.error_for("width").has_value());
    CHECK(validation.error_for("initialFill").has_value());
    CHECK(validation.destination ==
          project.path() / "assets/vectors/occupied.vector.json");
}

TEST_CASE("project and artwork prompt states are isolated and cancellable") {
    fabric::editor::CreateProjectPrompt project_prompt;
    fabric::editor::CreateVectorArtworkPrompt artwork_prompt;
    project_prompt.name = "Project state";
    artwork_prompt.name = "Artwork state";
    artwork_prompt.id = "artwork-state";

    project_prompt.reset();
    CHECK(project_prompt.name.empty());
    CHECK(artwork_prompt.name == "Artwork state");
    CHECK(artwork_prompt.id == "artwork-state");

    artwork_prompt.reset();
    CHECK(artwork_prompt.name.empty());
    CHECK(artwork_prompt.width == 10.0);
    CHECK(project_prompt.pixels_per_unit == 100.0);
}
