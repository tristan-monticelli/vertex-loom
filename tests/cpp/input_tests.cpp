#include "fabric/runtime/input.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("input action map tracks held, pressed and released bindings") {
    fabric::runtime::InputActionMap input;
    REQUIRE(input.define_action("jump"));
    REQUIRE(input.bind("jump", {fabric::runtime::InputDevice::keyboard, 32}));
    REQUIRE(input.bind("jump", {fabric::runtime::InputDevice::gamepad, 0}));
    REQUIRE_FALSE(input.define_action("jump"));

    input.press(fabric::runtime::InputDevice::keyboard, 32);
    CHECK(input.held("jump"));
    CHECK(input.pressed("jump"));
    CHECK_FALSE(input.released("jump"));
    input.begin_frame();
    CHECK(input.held("jump"));
    CHECK_FALSE(input.pressed("jump"));
    input.release(fabric::runtime::InputDevice::keyboard, 32);
    CHECK_FALSE(input.held("jump"));
    CHECK(input.released("jump"));
}

TEST_CASE("input action map ignores repeats and unknown actions") {
    fabric::runtime::InputActionMap input;
    REQUIRE(input.define_action("move"));
    REQUIRE(input.bind("move", {fabric::runtime::InputDevice::keyboard, 65}));
    input.press(fabric::runtime::InputDevice::keyboard, 65, true);
    CHECK(input.held("move"));
    CHECK_FALSE(input.pressed("move"));
    CHECK_FALSE(input.bind("missing", {fabric::runtime::InputDevice::keyboard, 1}));
}
