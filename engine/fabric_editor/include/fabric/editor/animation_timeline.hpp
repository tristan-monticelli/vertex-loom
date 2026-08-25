#pragma once

#include "fabric/editor/command_stack.hpp"
#include "fabric/project/animation.hpp"

#include <cstddef>

namespace fabric::editor {

class AnimationTimeline {
public:
    AnimationTimeline(project::AnimationClip& clip, CommandStack& commands) noexcept;

    [[nodiscard]] bool insert_key(const project::PropertyBinding& binding,
                                  float time, project::AnimationValue value,
                                  project::AnimationInterpolation interpolation);
    [[nodiscard]] bool move_key(const project::PropertyBinding& binding,
                                std::size_t key_index, float time);
    [[nodiscard]] bool remove_key(const project::PropertyBinding& binding,
                                  std::size_t key_index);
    [[nodiscard]] bool set_duration(float duration);
    [[nodiscard]] bool set_loop(bool loop);

private:
    project::AnimationClip& clip_;
    CommandStack& commands_;
};

} // namespace fabric::editor
