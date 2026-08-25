#include "fabric/editor/animation_timeline.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace fabric::editor {
namespace {

class AnimationSnapshotCommand final : public Command {
public:
    AnimationSnapshotCommand(project::AnimationClip& target,
                             project::AnimationClip before,
                             project::AnimationClip after, bool mergeable)
        : target_(target), before_(std::move(before)), after_(std::move(after)),
          mergeable_(mergeable) {}

    bool execute() override {
        target_ = after_;
        return true;
    }

    bool undo() override {
        target_ = before_;
        return true;
    }

    bool merge_with(const Command& newer) override {
        const auto* next = dynamic_cast<const AnimationSnapshotCommand*>(&newer);
        if (!next || !mergeable_ || !next->mergeable_ || &next->target_ != &target_)
            return false;
        after_ = next->after_;
        return true;
    }

private:
    project::AnimationClip& target_;
    project::AnimationClip before_;
    project::AnimationClip after_;
    bool mergeable_{};
};

project::AnimationTrack* find_track(project::AnimationClip& clip,
                                     const project::PropertyBinding& binding) {
    for (auto& track : clip.tracks)
        if (track.binding == binding)
            return &track;
    return nullptr;
}

bool same_value_type(const project::AnimationValue& left,
                     const project::AnimationValue& right) {
    return left.index() == right.index();
}

bool commit(CommandStack& commands, project::AnimationClip& target,
            project::AnimationClip before, project::AnimationClip after,
            bool mergeable = false) {
    if (!project::validate_animation(project::ProjectManifest{}, after).ok())
        return false;
    return commands.execute(std::make_unique<AnimationSnapshotCommand>(
        target, std::move(before), std::move(after), mergeable));
}

} // namespace

AnimationTimeline::AnimationTimeline(project::AnimationClip& clip,
                                     CommandStack& commands) noexcept
    : clip_(clip), commands_(commands) {}

bool AnimationTimeline::insert_key(const project::PropertyBinding& binding,
                                   float time, project::AnimationValue value,
                                   project::AnimationInterpolation interpolation) {
    auto next = clip_;
    auto* track = find_track(next, binding);
    if (!track) {
        next.tracks.push_back({binding, interpolation, {{time, std::move(value)}}});
    } else {
        if (track->interpolation != interpolation)
            return false;
        if (!track->keys.empty() && !same_value_type(track->keys.front().value, value))
            return false;
        track->keys.push_back({time, std::move(value)});
        std::stable_sort(track->keys.begin(), track->keys.end(),
                         [](const auto& left, const auto& right) {
                             return left.time < right.time;
                         });
    }
    auto before = clip_;
    return commit(commands_, clip_, std::move(before), std::move(next));
}

bool AnimationTimeline::move_key(const project::PropertyBinding& binding,
                                 std::size_t key_index, float time) {
    auto next = clip_;
    auto* track = find_track(next, binding);
    if (!track || key_index >= track->keys.size())
        return false;
    track->keys[key_index].time = time;
    std::stable_sort(track->keys.begin(), track->keys.end(),
                     [](const auto& left, const auto& right) {
                         return left.time < right.time;
                     });
    auto before = clip_;
    return commit(commands_, clip_, std::move(before), std::move(next), true);
}

bool AnimationTimeline::remove_key(const project::PropertyBinding& binding,
                                   std::size_t key_index) {
    auto next = clip_;
    auto* track = find_track(next, binding);
    if (!track || key_index >= track->keys.size() || track->keys.size() == 1)
        return false;
    track->keys.erase(track->keys.begin() + static_cast<std::ptrdiff_t>(key_index));
    auto before = clip_;
    return commit(commands_, clip_, std::move(before), std::move(next));
}

bool AnimationTimeline::set_duration(float duration) {
    auto next = clip_;
    next.duration = duration;
    auto before = clip_;
    return commit(commands_, clip_, std::move(before), std::move(next));
}

bool AnimationTimeline::set_loop(bool loop) {
    auto next = clip_;
    next.loop = loop;
    auto before = clip_;
    return commit(commands_, clip_, std::move(before), std::move(next));
}

} // namespace fabric::editor
