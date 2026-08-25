#include "fabric/project/replay.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "replay-test"}, .name = "Replay Test"};
}

fabric::project::ReplayDocument replay() {
    return {.document = {.schema_version = 1, .type = "replay",
                         .id = {.value = "run-one"}, .name = "Run One"},
            .build = "build-42", .seed = 1234,
            .source_scene = fabric::project::ResourceReference{{.value = "main-scene"}, "scene"},
            .inputs = {{10, "jump", true, false}, {11, "jump", false, true}},
            .events = {{60, "checkpoint", "ok"}},
            .checkpoints = {{60, {{"player", 4096, -2048, 8192}}}}};
}

TEST_CASE("replay documents round trip with quantized checkpoints") {
    const auto source = replay();
    const auto parsed = fabric::project::parse_replay(manifest(), fabric::project::serialize_replay(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);
    CHECK(fabric::project::quantize_replay_position(1.25F) == 5120);
    CHECK(fabric::project::dequantize_replay_rotation(32768) == 0.5F);
}

TEST_CASE("replay validation requires ordered unique checkpoints") {
    auto invalid = replay();
    invalid.checkpoints.push_back(invalid.checkpoints.front());
    REQUIRE_FALSE(fabric::project::validate_replay(manifest(), invalid).ok());
}

} // namespace
