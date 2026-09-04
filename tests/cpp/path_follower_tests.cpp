#include "fabric/project/path_follower.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::TexturedPath line_path() {
    using namespace fabric::project;
    return {.commands = {{.kind = TexturedPathCommandKind::move,
                          .point = {0.0F, 0.0F}},
                         {.kind = TexturedPathCommandKind::line,
                          .point = {10.0F, 0.0F}}}};
}

TEST_CASE("path follower samples position and tangent by arc length") {
    const auto sample = fabric::project::sample_textured_path(line_path(), 0.5F);
    REQUIRE(sample.position.x == Catch::Approx(5.0F));
    REQUIRE(sample.position.y == Catch::Approx(0.0F));
    REQUIRE(sample.tangent.x == Catch::Approx(1.0F));
    REQUIRE(sample.tangent.y == Catch::Approx(0.0F));
}

TEST_CASE("path follower advances with bounded and looping progress") {
    const auto path = line_path();
    REQUIRE(fabric::project::advance_path_follower(
                path, 0.0F, 5.0F, 1.0F, false) == Catch::Approx(0.5F));
    REQUIRE(fabric::project::advance_path_follower(
                path, 0.9F, 5.0F, 1.0F, false) == Catch::Approx(1.0F));
    REQUIRE(fabric::project::advance_path_follower(
                path, 0.9F, 5.0F, 1.0F, true) == Catch::Approx(0.4F));
}

TEST_CASE("path follower samples cubic curves and closed paths") {
    using namespace fabric::project;
    TexturedPath path{.commands = {
        {.kind = TexturedPathCommandKind::move, .point = {0.0F, 0.0F}},
        {.kind = TexturedPathCommandKind::cubic, .point = {3.0F, 0.0F},
         .control1 = {0.0F, 3.0F}, .control2 = {3.0F, 3.0F}}},
        .closed = true};
    const auto sample = sample_textured_path(path, 0.5F, 16U);
    REQUIRE(sample.progress == Catch::Approx(0.5F));
    REQUIRE(sample.tangent.x * sample.tangent.x +
                sample.tangent.y * sample.tangent.y == Catch::Approx(1.0F));
}

} // namespace
