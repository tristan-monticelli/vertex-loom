#pragma once

#include "fabric/editor/command_stack.hpp"
#include "fabric/project/animation.hpp"
#include "fabric/project/property.hpp"

#include <cstddef>
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
                                      project::AnimationComposition::replace);
    [[nodiscard]] bool set_key(const project::PropertyBinding& binding,
                               float time, project::AnimationValue value,
                               project::AnimationInterpolation interpolation,
                               project::AnimationComposition composition =
                                   project::AnimationComposition::replace);
    [[nodiscard]] bool move_key(const project::PropertyBinding& binding,
                                std::size_t key_index, float time);
    [[nodiscard]] bool remove_key(const project::PropertyBinding& binding,
                                  std::size_t key_index);
    [[nodiscard]] bool insert_marker(std::string id, float time);
    [[nodiscard]] bool remove_marker(std::string_view id);
    [[nodiscard]] bool set_duration(float duration);
    [[nodiscard]] bool set_loop(bool loop);

private:
    project::AnimationClip& clip_;
    CommandStack& commands_;
};

} // namespace fabric::editor
