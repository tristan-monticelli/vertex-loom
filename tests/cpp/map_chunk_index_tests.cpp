#include "fabric/project/map_chunk_index.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("map chunk index deterministically queries visible instances") {
    using namespace fabric::project;
    MapDocument map;
    map.document.id = {.value = "large-map"};
    map.document.name = "Large Map";
    map.layers = {{"instances", "Instances", MapLayerKind::instances, true, false, 0.0F}};
    for (int index = 0; index < 100000; ++index) {
        const float x = static_cast<float>(index % 1000);
        const float y = static_cast<float>(index / 1000);
        map.instances.push_back({"instance-" + std::to_string(index),
                                 ResourceReference{{.value = "entity"}, "entity"},
                                 std::nullopt, "instances", {.position = {x, y}},
                                 static_cast<std::int32_t>(x / 64.0F),
                                 static_cast<std::int32_t>(y / 64.0F), {}});
    }
    MapChunkIndex index;
    REQUIRE(index.rebuild(map));
    REQUIRE(index.entries().size() == 100000);
    const auto visible = index.query({{0.0F, 0.0F}, {64.0F, 64.0F}});
    REQUIRE_FALSE(visible.empty());
    REQUIRE(visible.front() == "instance-0");
    REQUIRE(index.query({{-64.0F, -64.0F}, {1.0F, 1.0F}}).empty());
}

TEST_CASE("map chunk index uses floor for negative coordinates") {
    const auto chunk = fabric::project::MapChunkIndex::chunk_for({-0.1F, -64.1F});
    REQUIRE(chunk.first == -1);
    REQUIRE(chunk.second == -2);
}
