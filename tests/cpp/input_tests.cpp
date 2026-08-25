#include "fabric/runtime/input.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

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

TEST_CASE("input action map accepts logical gamepad buttons") {
    fabric::runtime::InputActionMap input;
    REQUIRE(input.define_action("jump"));
    REQUIRE(input.bind("jump", {fabric::runtime::InputDevice::gamepad, 0}));
    input.press(fabric::runtime::InputDevice::gamepad, 0);
    CHECK(input.held("jump"));
    CHECK(input.pressed("jump"));
    input.release(fabric::runtime::InputDevice::gamepad, 0);
    CHECK(input.released("jump"));
}

TEST_CASE("input action map configures a complete binding table atomically") {
    fabric::runtime::InputActionMap input;
    const std::vector<fabric::runtime::InputActionDefinition> definitions{
        {"move_left", {{fabric::runtime::InputDevice::keyboard, 65}}},
        {"jump", {{fabric::runtime::InputDevice::gamepad, 0}}}};
    REQUIRE(input.configure(definitions));
    REQUIRE(input.actions() == definitions);
    input.press(fabric::runtime::InputDevice::keyboard, 65);
    CHECK(input.held("move_left"));

    const std::vector<fabric::runtime::InputActionDefinition> invalid{
        {"duplicate", {{fabric::runtime::InputDevice::keyboard, 1}}},
        {"duplicate", {{fabric::runtime::InputDevice::keyboard, 2}}}};
    CHECK_FALSE(input.configure(invalid));
    CHECK(input.actions() == definitions);
    CHECK(input.held("move_left"));
}
