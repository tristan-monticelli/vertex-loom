#pragma once

#include "fabric/core/types.hpp"

#include <string_view>
#include <vector>

namespace fabric::project {

enum class TextureClassification { floor, rope, beam, button_eye, collision_marker };
enum class SurfaceShaderProfile { thread, plastic, monochrome, custom };
enum class SurfaceEffectKind { tint, holography, shine };

struct SurfaceEffect {
    SurfaceEffectKind kind{SurfaceEffectKind::tint};
    bool enabled{true};
    core::Color color{1.0F, 1.0F, 1.0F, 1.0F};
    float amount{1.0F};
    float scale{1.0F};

    friend bool operator==(const SurfaceEffect&, const SurfaceEffect&) = default;
};

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
    std::vector<SurfaceEffect> effects;

    friend bool operator==(const ShaderSurfaceSettings&, const ShaderSurfaceSettings&) = default;
};

[[nodiscard]] std::string_view to_string(TextureClassification) noexcept;
[[nodiscard]] std::string_view to_string(SurfaceShaderProfile) noexcept;
[[nodiscard]] std::string_view to_string(SurfaceEffectKind) noexcept;

} // namespace fabric::project
