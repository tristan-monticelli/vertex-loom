#include "fabric/project/progress_save.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <limits>

namespace {

fabric::project::ProgressSave save() {
    return {.schema_version = 1,
            .build = "build-7",
            .scene = {{.value = "main-scene"}, "scene"},
            .properties = {{"has-key", true},
                           {"coins", std::int64_t{12}},
                           {"volume", 0.75},
                           {"spawn", fabric::core::Vec2{1.0F, -2.0F}},
                           {"weapon", fabric::project::ResourceReference{{.value = "starter"}, "item"}}}};
}

TEST_CASE("progress save round trips typed properties atomically") {
    const auto source = save();
    const auto parsed = fabric::project::parse_progress_save(
        fabric::project::serialize_progress_save(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.save == source);

    const auto path = std::filesystem::temp_directory_path() /
        ("fabric-progress-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())) /
        "slot-1.json";
    REQUIRE(std::filesystem::create_directories(path.parent_path()));
    REQUIRE(fabric::project::save_progress_save_atomic(path, source).ok());
    const auto loaded = fabric::project::load_progress_save(path);
    REQUIRE(loaded.ok());
    CHECK(*loaded.save == source);
    std::error_code ignored;
    std::filesystem::remove_all(path.parent_path(), ignored);
}

TEST_CASE("progress save rejects non-finite values and invalid scene references") {
    auto invalid = save();
    invalid.scene.expected_type = "map";
    invalid.properties["volume"] = std::numeric_limits<double>::infinity();
    REQUIRE_FALSE(fabric::project::validate_progress_save(invalid).ok());
}

} // namespace
