#include "fabric/project/shader_profile.hpp"

namespace fabric::project {

std::string_view to_string(const TextureClassification value) noexcept {
    switch (value) {
    case TextureClassification::floor: return "floor";
    case TextureClassification::rope: return "rope";
    case TextureClassification::beam: return "beam";
    case TextureClassification::button_eye: return "buttonEye";
    case TextureClassification::collision_marker: return "collisionMarker";
    }
    return "rope";
}

std::string_view to_string(const SurfaceShaderProfile value) noexcept {
    switch (value) {
    case SurfaceShaderProfile::thread: return "Thread";
    case SurfaceShaderProfile::plastic: return "Plastic";
    case SurfaceShaderProfile::monochrome: return "Monochrome";
    case SurfaceShaderProfile::custom: return "Custom";
    }
    return "Monochrome";
}

std::string_view to_string(const SurfaceEffectKind value) noexcept {
    switch (value) {
    case SurfaceEffectKind::tint: return "Tint";
    case SurfaceEffectKind::holography: return "Holography";
    case SurfaceEffectKind::shine: return "Shine";
    }
    return "Tint";
}

} // namespace fabric::project
