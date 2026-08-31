#pragma once

#include "fabric/core/types.hpp"

#include <string_view>

namespace fabric::project {

enum class TextureClassification { floor, rope, beam, button_eye, collision_marker };
enum class SurfaceShaderProfile { thread, plastic, monochrome, custom };

struct ShaderSurfaceSettings {
    SurfaceShaderProfile profile{SurfaceShaderProfile::monochrome};
    TextureClassification classification{TextureClassification::rope};
    core::Color primary_color{1.0F, 1.0F, 1.0F, 1.0F};
    core::Color effect_color{1.0F, 1.0F, 1.0F, 1.0F};
    float shine{};
    float holography{};
    float opacity{1.0F};
    float intensity{1.0F};
    core::Vec2 repetition{1.0F, 1.0F};
    core::Vec2 deformation{};

    friend bool operator==(const ShaderSurfaceSettings&, const ShaderSurfaceSettings&) = default;
};

[[nodiscard]] std::string_view to_string(TextureClassification) noexcept;
[[nodiscard]] std::string_view to_string(SurfaceShaderProfile) noexcept;

} // namespace fabric::project
