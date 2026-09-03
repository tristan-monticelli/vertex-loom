#include "fabric/project/document_storage.hpp"
#include "fabric/project/input.hpp"
#include "fabric/project/audio.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace {

class TemporaryProject {
public:
    TemporaryProject() {
        root_ = std::filesystem::temp_directory_path() /
            ("fabric-input-document-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_);
    }
    ~TemporaryProject() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    const std::filesystem::path& root() const noexcept { return root_; }
private:
    std::filesystem::path root_;
};

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "input-test"},
            .name = "Input Test",
            .directories = {}};
}

fabric::project::InputDocument example() {
    return {.document = {.schema_version = fabric::project::current_input_schema_version,
                          .type = "input",
                          .id = {.value = "default"},
                          .name = "Default Input"},
            .actions = {
                {"move_left", {{fabric::project::InputDevice::keyboard, 65}}},
                {"jump", {{fabric::project::InputDevice::gamepad, 0}}}}};
}

} // namespace

TEST_CASE("input document round-trips and publishes atomically") {
    const auto expected = example();
    const auto parsed = fabric::project::parse_input(
        fabric::project::serialize_input(expected));
    REQUIRE(parsed.ok());
    CHECK(*parsed.input == expected);

    TemporaryProject project;
    const auto published = fabric::project::publish_input(
        project.root(), manifest(), expected);
    REQUIRE(published.ok());
    CHECK(std::filesystem::is_regular_file(
        project.root() / fabric::project::input_document_path(manifest(), expected.document.id)));
    CHECK(*published.input == expected);
}

TEST_CASE("input document rejects duplicate and invalid bindings") {
    const auto parsed = fabric::project::parse_input(R"({
        "schemaVersion": 1,
        "type": "input",
        "id": "default",
        "name": "Default",
        "actions": [
            {"id": "jump", "bindings": [
                {"device": "keyboard", "code": 32},
                {"device": "keyboard", "code": 32}
            ]},
            {"id": "jump", "bindings": [
                {"device": "joystick", "code": 1}
            ]}
        ]
    })");
    CHECK_FALSE(parsed.ok());
    CHECK(parsed.errors.size() >= 3);
}

TEST_CASE("input document rejects a mismatched filename") {
    TemporaryProject project;
    const auto input = example();
    const auto path = std::filesystem::path{"assets/input/other.input.json"};
    const auto saved = fabric::project::save_document_atomic(
        project.root(), path, fabric::project::serialize_input(input),
        [](const std::string_view text) {
            const auto parsed = fabric::project::parse_input(text);
            return fabric::project::ValidationReport{.errors = parsed.errors};
        });
    REQUIRE(saved.ok());
    const auto loaded = fabric::project::load_input(project.root(), manifest(), path);
    CHECK_FALSE(loaded.ok());
    CHECK(std::any_of(loaded.errors.begin(), loaded.errors.end(), [](const auto& error) {
        return error.code == fabric::project::ErrorCode::invalid_path;
    }));
}

TEST_CASE("input document round-trips axis thresholds and modifiers") {
    auto input = example();
    input.actions[0].bindings[0].kind = fabric::project::InputBindingKind::axis;
    input.actions[0].bindings[0].threshold = 0.75F;
    input.actions[0].bindings[0].dead_zone = 0.2F;
    input.actions[0].bindings[0].ctrl = true;
    input.actions[0].bindings[0].shift = true;
    const auto parsed = fabric::project::parse_input(fabric::project::serialize_input(input));
    REQUIRE(parsed.ok());
    CHECK(*parsed.input == input);
}

TEST_CASE("audio document round-trips events") {
    fabric::project::AudioDocument audio;
    audio.document.id = {.value = "music"};
    audio.document.name = "Music";
    audio.buses = {{"music", 0.6F}};
    audio.events = {{.id = "theme", .source = "audio/theme.wav",
                     .volume = 0.8F, .loop = true, .bus = "music",
                     .spatial = fabric::project::AudioSpatialSettings{
                         .position = {4.0F, 3.0F}, .minimum_distance = 2.0F,
                         .maximum_distance = 12.0F}}};
    const auto parsed = fabric::project::parse_audio(
        fabric::project::serialize_audio(audio));
    REQUIRE(parsed.ok());
    CHECK(*parsed.audio == audio);
}

TEST_CASE("audio v1 migrates to master bus without spatialization") {
    const auto parsed = fabric::project::parse_audio(
        R"({"schemaVersion":1,"type":"audio","id":"legacy-audio","name":"Legacy","events":[{"id":"theme","source":"audio/theme.wav","volume":0.8,"loop":true}]})");
    REQUIRE(parsed.ok());
    CHECK(parsed.audio->document.schema_version ==
          fabric::project::current_audio_schema_version);
    REQUIRE(parsed.audio->events.size() == 1U);
    CHECK(parsed.audio->events.front().bus == "master");
    CHECK_FALSE(parsed.audio->events.front().spatial.has_value());
}

TEST_CASE("audio rejects traversal and missing buses") {
    fabric::project::AudioDocument audio;
    audio.document.id = {.value = "unsafe-audio"};
    audio.document.name = "Unsafe";
    audio.events = {{.id = "escape", .source = "../outside.wav",
                     .bus = "missing"}};
    const auto report = fabric::project::validate_audio(manifest(), audio);
    CHECK_FALSE(report.ok());
    CHECK(std::ranges::any_of(report.errors, [](const auto& error) {
        return error.field == "events[0].source";
    }));
    CHECK(std::ranges::any_of(report.errors, [](const auto& error) {
        return error.field == "events[0].bus";
    }));
}
