#include "fabric/editor/transformation_session.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/manifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {
fabric::project::ProjectManifest manifest() {
    fabric::project::ProjectManifest value;
    value.id = {.value = "transformation-session"};
    value.name = "Transformation Session";
    return value;
}

fabric::project::EntityDefinition entity(std::string id) {
    fabric::project::EntityDefinition value;
    value.document.id = {.value = std::move(id)};
    value.document.name = value.document.id.value;
    return value;
}

fabric::project::EntityTransformation transformation() {
    fabric::project::EntityTransformation value;
    value.document.id = {.value = "hero-to-beast"};
    value.document.name = "Hero to Beast";
    value.source_entity = {{.value = "hero"}, "entity"};
    value.destination_entity = {{.value = "beast"}, "entity"};
    return value;
}

std::filesystem::path temporary_root() {
    return std::filesystem::temp_directory_path() /
        ("fabric-transformation-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

void prepare(const std::filesystem::path& root) {
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity("hero")).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity("beast")).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity("bird")).ok());
}
} // namespace

TEST_CASE("transformation session edits policies with undo save and reload") {
    const auto root = temporary_root();
    prepare(root);
    fabric::editor::TransformationSession session;
    REQUIRE(session.create(root, transformation()));
    auto policy = session.transformation()->policy;
    policy.properties = fabric::project::TransferMode::mapping;
    policy.mappings.push_back({fabric::project::TransferDomain::property,
                               "health", "hit-points"});
    REQUIRE(session.set_policy(policy));
    REQUIRE(session.set_destination({.value = "bird"}));
    REQUIRE(session.undo());
    CHECK(session.transformation()->destination_entity.id.value == "beast");
    REQUIRE(session.redo());
    REQUIRE(session.save());

    fabric::editor::TransformationSession reopened;
    REQUIRE(reopened.open(root, {.value = "hero-to-beast"}));
    CHECK(reopened.transformation()->destination_entity.id.value == "bird");
    CHECK(reopened.transformation()->policy == policy);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("transformation session rejects missing endpoints and recovers autosave") {
    using namespace std::chrono_literals;
    const auto root = temporary_root();
    prepare(root);
    fabric::editor::TransformationSession session;
    REQUIRE(session.create(root, transformation()));
    CHECK_FALSE(session.set_destination({.value = "missing"}));
    CHECK(session.transformation()->destination_entity.id.value == "beast");
    auto policy = session.transformation()->policy;
    policy.physics = fabric::project::TransferMode::preserve;
    REQUIRE(session.set_policy(policy));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    CHECK(session.update_autosave(start) == fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(start + 2s) == fabric::editor::AutosaveStatus::saved);

    fabric::editor::TransformationSession reopened;
    REQUIRE(reopened.open(root, {.value = "hero-to-beast"}));
    REQUIRE(reopened.has_recovery());
    REQUIRE(reopened.accept_recovery(start + 3s));
    CHECK(reopened.transformation()->policy.physics ==
          fabric::project::TransferMode::preserve);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
