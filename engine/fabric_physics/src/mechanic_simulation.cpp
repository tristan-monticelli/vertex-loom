#include "fabric/physics/mechanic_simulation.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace fabric::physics {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr float fixed_step = 1.0F / 60.0F;
constexpr int sub_steps = 4;

float radians(const float degrees) noexcept { return degrees * pi / 180.0F; }
float degrees(const float radians_value) noexcept { return radians_value * 180.0F / pi; }

b2BodyType body_type(const MechanicBodyType type) noexcept {
    switch (type) {
    case MechanicBodyType::static_body: return b2_staticBody;
    case MechanicBodyType::kinematic_body: return b2_kinematicBody;
    case MechanicBodyType::dynamic_body: return b2_dynamicBody;
    }
    return b2_staticBody;
}

} // namespace

struct MechanicSimulation::Impl {
    MechanicPlan plan;
    b2WorldId world{b2_nullWorldId};
    b2BodyId ground{b2_nullBodyId};
    std::unordered_map<std::string, b2BodyId> bodies;
    std::unordered_map<std::string, b2JointId> joints;
    std::unordered_map<std::string, float> motor_speeds;
    std::vector<MechanicBodyState> states;
    std::vector<MechanicSignalState> signals;
    bool playing{};
    bool has_plan{};
    float accumulator{};
    std::size_t steps{};

    void destroy() noexcept {
        if (b2World_IsValid(world)) b2DestroyWorld(world);
        world = b2_nullWorldId;
        ground = b2_nullBodyId;
        bodies.clear();
        joints.clear();
        motor_speeds.clear();
        states.clear();
        signals.clear();
        accumulator = 0.0F;
        steps = 0;
    }

    const MechanicPivotDescription* pivot(const std::string& id) const {
        const auto found = std::ranges::find(plan.pivots, id,
                                             &MechanicPivotDescription::node_id);
        return found == plan.pivots.end() ? nullptr : &*found;
    }

    b2BodyId body(const std::optional<std::string>& id) const {
        if (!id) return ground;
        const auto found = bodies.find(*id);
        return found == bodies.end() ? b2_nullBodyId : found->second;
    }

    void refresh_states() {
        states.clear();
        states.reserve(plan.bodies.size());
        for (const auto& description : plan.bodies) {
            const auto found = bodies.find(description.node_id);
            if (found == bodies.end() || !b2Body_IsValid(found->second)) continue;
            const auto position = b2Body_GetPosition(found->second);
            states.push_back({
                .node_id = description.node_id,
                .position = {position.x, position.y},
                .rotation_degrees = degrees(
                    b2Rot_GetAngle(b2Body_GetRotation(found->second))) });
        }
    }

    bool source_active(const std::optional<std::string>& source) const noexcept {
        if (!source) return true;
        const auto found = std::ranges::find(
            signals, *source, &MechanicSignalState::node_id);
        return found != signals.end() && found->active;
    }

    void update_motors(const float time_step) {
        for (const auto& motor : plan.motors) {
            const auto joint = joints.find(motor.joint_node_id);
            if (joint == joints.end()) continue;
            const auto enabled = source_active(motor.enabled_source_node_id);
            const auto target = enabled
                ? radians(motor.speed_degrees_per_second *
                          static_cast<float>(motor.direction))
                : 0.0F;
            auto& current = motor_speeds[motor.node_id];
            const auto acceleration = radians(
                motor.acceleration_degrees_per_second_squared);
            if (acceleration <= 0.0F) {
                current = target;
            } else {
                const auto maximum_delta = acceleration * time_step;
                current += std::clamp(target - current,
                                      -maximum_delta, maximum_delta);
            }
            b2RevoluteJoint_EnableMotor(joint->second, true);
            b2RevoluteJoint_SetMotorSpeed(joint->second, current);
        }
    }

    bool build() {
        destroy();
        auto world_definition = b2DefaultWorldDef();
        world_definition.gravity = {0.0F, -9.81F};
        world = b2CreateWorld(&world_definition);
        if (!b2World_IsValid(world)) return false;

        auto ground_definition = b2DefaultBodyDef();
        ground_definition.type = b2_staticBody;
        ground = b2CreateBody(world, &ground_definition);
        if (!b2Body_IsValid(ground)) { destroy(); return false; }

        for (const auto& description : plan.bodies) {
            auto definition = b2DefaultBodyDef();
            definition.type = body_type(description.type);
            definition.position = {description.position.x, description.position.y};
            definition.rotation = b2MakeRot(radians(description.rotation_degrees));
            definition.enableSleep = false;
            const auto body_id = b2CreateBody(world, &definition);
            if (!b2Body_IsValid(body_id)) { destroy(); return false; }
            auto shape_definition = b2DefaultShapeDef();
            shape_definition.density = description.density;
            shape_definition.material.friction = description.friction;
            const auto box = b2MakeBox(description.size.x * 0.5F,
                                       description.size.y * 0.5F);
            if (!b2Shape_IsValid(b2CreatePolygonShape(
                    body_id, &shape_definition, &box))) {
                destroy();
                return false;
            }
            bodies.emplace(description.node_id, body_id);
        }

        for (const auto& description : plan.joints) {
            const auto body_a = body(description.body_a_node_id);
            const auto body_b = body(description.body_b_node_id);
            const auto* pivot_description = description.pivot_node_id
                ? pivot(*description.pivot_node_id) : nullptr;
            if (!b2Body_IsValid(body_a) || !b2Body_IsValid(body_b) ||
                pivot_description == nullptr) {
                destroy();
                return false;
            }
            const b2Vec2 anchor{pivot_description->position.x,
                                pivot_description->position.y};
            auto definition = b2DefaultRevoluteJointDef();
            definition.bodyIdA = body_a;
            definition.bodyIdB = body_b;
            definition.localAnchorA = b2Body_GetLocalPoint(body_a, anchor);
            definition.localAnchorB = b2Body_GetLocalPoint(body_b, anchor);
            definition.enableLimit = description.limit_enabled;
            definition.lowerAngle = radians(description.minimum_angle_degrees);
            definition.upperAngle = radians(description.maximum_angle_degrees);
            const auto joint_id = b2CreateRevoluteJoint(world, &definition);
            if (!b2Joint_IsValid(joint_id)) { destroy(); return false; }
            joints.emplace(description.node_id, joint_id);
        }

        for (const auto& motor : plan.motors) {
            const auto found = joints.find(motor.joint_node_id);
            if (found == joints.end()) { destroy(); return false; }
            b2RevoluteJoint_EnableMotor(found->second, true);
            b2RevoluteJoint_SetMotorSpeed(found->second, 0.0F);
            b2RevoluteJoint_SetMaxMotorTorque(found->second, motor.maximum_torque);
            motor_speeds.emplace(motor.node_id, 0.0F);
        }

        for (const auto& sensor : plan.sensors)
            signals.push_back({.node_id = sensor.node_id,
                               .kind = MechanicSignalKind::sensor});
        for (const auto& event : plan.events)
            if (event.mode == MechanicEventMode::listen)
                signals.push_back({.node_id = event.node_id,
                                   .kind = MechanicSignalKind::event,
                                   .event_id = event.event_id});

        for (const auto& constraint : plan.constraints) {
            const auto body_id = body(std::optional<std::string>{constraint.body_node_id});
            const auto* pivot_description = constraint.pivot_node_id
                ? pivot(*constraint.pivot_node_id) : nullptr;
            if (!b2Body_IsValid(body_id) || pivot_description == nullptr) continue;
            const auto existing = std::ranges::find_if(
                plan.joints, [&](const auto& joint) {
                    return joint.body_a_node_id == constraint.body_node_id &&
                           joint.pivot_node_id == constraint.pivot_node_id;
                });
            if (existing != plan.joints.end()) {
                const auto joint = joints.find(existing->node_id);
                if (joint != joints.end()) {
                    b2RevoluteJoint_EnableLimit(joint->second, true);
                    b2RevoluteJoint_SetLimits(
                        joint->second, radians(constraint.minimum_angle_degrees),
                        radians(constraint.maximum_angle_degrees));
                }
            }
        }
        refresh_states();
        return true;
    }
};

MechanicSimulation::MechanicSimulation() : impl_(std::make_unique<Impl>()) {}
MechanicSimulation::~MechanicSimulation() = default;
MechanicSimulation::MechanicSimulation(MechanicSimulation&&) noexcept = default;
MechanicSimulation& MechanicSimulation::operator=(MechanicSimulation&&) noexcept = default;

bool MechanicSimulation::load(MechanicPlan plan) {
    impl_->plan = std::move(plan);
    impl_->has_plan = true;
    impl_->playing = false;
    return impl_->build();
}

bool MechanicSimulation::reset() {
    impl_->playing = false;
    return impl_->has_plan && impl_->build();
}

bool MechanicSimulation::update(const float frame_seconds) {
    if (!valid() || !impl_->playing || !std::isfinite(frame_seconds) ||
        frame_seconds < 0.0F) return false;
    impl_->accumulator += std::min(frame_seconds, 0.25F);
    while (impl_->accumulator >= fixed_step) {
        impl_->update_motors(fixed_step);
        b2World_Step(impl_->world, fixed_step, sub_steps);
        impl_->accumulator -= fixed_step;
        ++impl_->steps;
    }
    impl_->refresh_states();
    return true;
}

bool MechanicSimulation::step_once() {
    if (!valid() || impl_->playing) return false;
    impl_->update_motors(fixed_step);
    b2World_Step(impl_->world, fixed_step, sub_steps);
    ++impl_->steps;
    impl_->refresh_states();
    return true;
}

void MechanicSimulation::play() noexcept { if (valid()) impl_->playing = true; }
void MechanicSimulation::pause() noexcept { impl_->playing = false; }
bool MechanicSimulation::set_sensor_active(
    const core::ResourceId& node_id, const bool active) noexcept {
    const auto found = std::ranges::find(
        impl_->signals, node_id.value, &MechanicSignalState::node_id);
    if (found == impl_->signals.end() ||
        found->kind != MechanicSignalKind::sensor) return false;
    found->active = active;
    return true;
}
bool MechanicSimulation::set_event_active(
    const core::ResourceId& event_id, const bool active) noexcept {
    bool changed = false;
    for (auto& signal : impl_->signals) {
        if (signal.kind == MechanicSignalKind::event &&
            signal.event_id == event_id) {
            signal.active = active;
            changed = true;
        }
    }
    return changed;
}
bool MechanicSimulation::valid() const noexcept {
    return impl_ && b2World_IsValid(impl_->world);
}
bool MechanicSimulation::playing() const noexcept { return impl_->playing; }
std::size_t MechanicSimulation::step_count() const noexcept { return impl_->steps; }
const std::vector<MechanicBodyState>& MechanicSimulation::body_states() const noexcept {
    return impl_->states;
}
const std::vector<MechanicSignalState>& MechanicSimulation::signal_states() const noexcept {
    return impl_->signals;
}

} // namespace fabric::physics
