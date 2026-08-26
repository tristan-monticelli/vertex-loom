#include "fabric/project/document_storage.hpp"
#include "fabric/project/input.hpp"

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
