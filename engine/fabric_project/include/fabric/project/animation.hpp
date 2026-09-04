#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_animation_schema_version = 4;

using AnimationValue = std::variant<float, core::Vec2, core::Color, bool,
                                     ResourceReference>;

enum class AnimationInterpolation { step, linear, cubic };
[[nodiscard]] std::string_view to_string(AnimationInterpolation) noexcept;

enum class AnimationEasing { linear, ease_in, ease_out, ease_in_out };
[[nodiscard]] std::string_view to_string(AnimationEasing) noexcept;

enum class AnimationComposition { replace, additive };
[[nodiscard]] std::string_view to_string(AnimationComposition) noexcept;

struct PropertyBinding {
    std::string node_id;
    std::string component_id;
    std::string property_id;
    friend bool operator==(const PropertyBinding&, const PropertyBinding&) = default;
};

struct AnimationKey {
    float time{};
    AnimationValue value{0.0F};
    std::optional<AnimationValue> in_tangent;
    std::optional<AnimationValue> out_tangent;
    friend bool operator==(const AnimationKey&, const AnimationKey&) = default;
};

struct AnimationTrack {
    PropertyBinding binding;
    AnimationInterpolation interpolation{AnimationInterpolation::linear};
    std::vector<AnimationKey> keys;
    AnimationComposition composition{AnimationComposition::replace};
    AnimationEasing easing{AnimationEasing::linear};
    friend bool operator==(const AnimationTrack&, const AnimationTrack&) = default;
};

struct AnimationAudioCue {
    ResourceReference audio;
    std::string event_id;
    friend bool operator==(const AnimationAudioCue&,
                           const AnimationAudioCue&) = default;
};

struct AnimationMarker {
    std::string id;
    float time{};
    std::optional<AnimationAudioCue> audio;
    friend bool operator==(const AnimationMarker&, const AnimationMarker&) = default;
};

struct AnimationMarkerHit {
    std::string id;
    float time{};
    float local_time{};
    std::int64_t loop_index{};
    std::optional<AnimationAudioCue> audio;
    friend bool operator==(const AnimationMarkerHit&, const AnimationMarkerHit&) = default;
};

struct AnimationClip {
    DocumentHeader document{
        .schema_version = current_animation_schema_version,
        .type = "animation",
    };
    std::optional<ResourceReference> preview_entity;
    float duration{};
    bool loop{};
    std::vector<AnimationMarker> markers;
    std::vector<AnimationTrack> tracks;
    friend bool operator==(const AnimationClip&, const AnimationClip&) = default;
};

struct AnimationResult {
    std::optional<AnimationClip> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return asset.has_value() && errors.empty(); }
};

struct EvaluatedProperty {
    PropertyBinding binding;
    AnimationValue value;
    AnimationComposition composition{AnimationComposition::replace};
    friend bool operator==(const EvaluatedProperty&, const EvaluatedProperty&) = default;
};

struct EvaluationResult {
    std::vector<EvaluatedProperty> properties;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] std::filesystem::path animation_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_animation(
    const ProjectManifest&, const AnimationClip&);
[[nodiscard]] std::vector<ResourceReference> animation_resource_references(
    const AnimationClip&);
[[nodiscard]] std::string serialize_animation(const AnimationClip&);
[[nodiscard]] AnimationResult parse_animation(const ProjectManifest&, std::string_view);
[[nodiscard]] AnimationResult load_animation(const std::filesystem::path&,
                                             const ProjectManifest&,
                                             const std::filesystem::path&);
[[nodiscard]] AnimationResult publish_animation(const std::filesystem::path&,
                                                const ProjectManifest&,
                                                const AnimationClip&);
[[nodiscard]] EvaluationResult evaluate_animation(const AnimationClip&, float time);
[[nodiscard]] std::vector<AnimationMarkerHit> animation_markers_between(
    const AnimationClip&, float from_time, float to_time);

} // namespace fabric::project
