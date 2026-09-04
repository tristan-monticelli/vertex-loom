#pragma once

#include "fabric/editor/command_stack.hpp"
#include "fabric/project/animation.hpp"
#include "fabric/project/property.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace fabric::editor {

class AnimationTimeline {
public:
    AnimationTimeline(project::AnimationClip& clip, CommandStack& commands) noexcept;

    [[nodiscard]] static std::vector<project::PropertyBinding> animatable_bindings(
        std::string_view node_id, const project::PropertyDescriptorRegistry& registry);

    [[nodiscard]] bool insert_key(const project::PropertyBinding& binding,
                                  float time, project::AnimationValue value,
                                  project::AnimationInterpolation interpolation,
                                  project::AnimationComposition composition =
                                      project::AnimationComposition::replace,
                                project::AnimationEasing easing =
                                    project::AnimationEasing::linear,
                                std::optional<project::AnimationValue> in_tangent = {},
                                std::optional<project::AnimationValue> out_tangent = {});
    [[nodiscard]] bool set_key(const project::PropertyBinding& binding,
                               float time, project::AnimationValue value,
                               project::AnimationInterpolation interpolation,
                               project::AnimationComposition composition =
                                   project::AnimationComposition::replace,
                               project::AnimationEasing easing =
                                   project::AnimationEasing::linear,
                               std::optional<project::AnimationValue> in_tangent = {},
                               std::optional<project::AnimationValue> out_tangent = {},
                               bool mergeable = false);
    [[nodiscard]] bool set_segment(const project::PropertyBinding& binding,
                                   float start_time,
                                   project::AnimationValue start_value,
                                   float end_time,
                                   project::AnimationValue end_value,
                                   project::AnimationInterpolation interpolation,
                                   project::AnimationComposition composition =
                                       project::AnimationComposition::replace,
                                   project::AnimationEasing easing =
                                       project::AnimationEasing::linear);
    [[nodiscard]] bool move_key(const project::PropertyBinding& binding,
                                std::size_t key_index, float time);
    [[nodiscard]] bool set_track_curve(
        const project::PropertyBinding& binding,
        project::AnimationInterpolation interpolation,
        project::AnimationEasing easing);
    [[nodiscard]] bool remove_key(const project::PropertyBinding& binding,
                                  std::size_t key_index);
    [[nodiscard]] bool insert_marker(
        std::string id, float time,
        std::optional<project::AnimationAudioCue> audio = std::nullopt);
    [[nodiscard]] bool remove_marker(std::string_view id);
    [[nodiscard]] bool set_duration(float duration);
    [[nodiscard]] bool set_loop(bool loop);

private:
    project::AnimationClip& clip_;
    CommandStack& commands_;
};

} // namespace fabric::editor
