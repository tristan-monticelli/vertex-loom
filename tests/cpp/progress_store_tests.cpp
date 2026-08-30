#include "fabric/runtime/progress_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {

fabric::project::ProgressSave save() {
    return {.schema_version = 1,
            .build = "runtime-build",
            .scene = {{.value = "start"}, "scene"},
            .properties = {{"visited", true}}};
}

TEST_CASE("progress store persists through an injected user slot") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-progress-store-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directories(root));
    fabric::runtime::ProgressStore store;
    REQUIRE(store.configure_path(root / "slot.json"));
    REQUIRE(store.save(save()));
    fabric::project::ProgressSave loaded;
    REQUIRE(store.load(loaded));
    CHECK(loaded == save());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("progress store rejects path traversal and unconfigured access") {
    fabric::runtime::ProgressStore store;
    CHECK_FALSE(store.configure_path("../slot.json"));
    fabric::project::ProgressSave loaded;
    CHECK_FALSE(store.load(loaded));
    CHECK_FALSE(store.save({}));
}

} // namespace
