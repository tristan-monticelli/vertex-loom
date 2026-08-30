#pragma once

namespace fabric::core {

struct Vec2 {
    float x{};
    float y{};

    friend bool operator==(const Vec2&, const Vec2&) = default;
};

struct Color {
    float red{1.0F};
    float green{1.0F};
    float blue{1.0F};
    float alpha{1.0F};

    friend bool operator==(const Color&, const Color&) = default;
};

struct Rect {
    Vec2 origin;
    Vec2 size;

    friend bool operator==(const Rect&, const Rect&) = default;
};

struct Transform {
    Vec2 position;
    float rotation_degrees{};
    Vec2 scale{1.0F, 1.0F};
    Vec2 pivot;

    friend bool operator==(const Transform&, const Transform&) = default;
};

} // namespace fabric::core
