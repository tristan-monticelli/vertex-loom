#include "fabric/physics/physics_world.hpp"

#include "fabric/project/map.hpp"

#include <box2d/box2d.h>

#include <new>
#include <vector>

namespace fabric::physics {

struct PhysicsWorld::Impl {
    b2WorldId id = b2_nullWorldId;
    b2BodyId collision_body = b2_nullBodyId;
    b2BodyId character_body = b2_nullBodyId;
};

PhysicsWorld::~PhysicsWorld() { destroy(); }

PhysicsWorld::PhysicsWorld(PhysicsWorld&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&& other) noexcept {
    if (this == &other) return *this;
    destroy();
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

bool PhysicsWorld::create(const float gravity_x, const float gravity_y) noexcept {
    destroy();
    impl_ = new (std::nothrow) Impl{};
    if (!impl_) return false;
    auto definition = b2DefaultWorldDef();
    definition.gravity = {gravity_x, gravity_y};
    impl_->id = b2CreateWorld(&definition);
    if (!b2World_IsValid(impl_->id)) {
        destroy();
        return false;
    }
    return true;
}

void PhysicsWorld::destroy() noexcept {
    if (!impl_) return;
    if (b2Body_IsValid(impl_->character_body)) b2DestroyBody(impl_->character_body);
    if (b2Body_IsValid(impl_->collision_body)) b2DestroyBody(impl_->collision_body);
    if (b2World_IsValid(impl_->id)) b2DestroyWorld(impl_->id);
    delete impl_;
    impl_ = nullptr;
}

bool PhysicsWorld::valid() const noexcept {
    return impl_ && b2World_IsValid(impl_->id);
}

bool PhysicsWorld::step(const float time_step, const std::size_t sub_steps) noexcept {
    if (!valid() || time_step <= 0.0F || sub_steps == 0) return false;
    b2World_Step(impl_->id, time_step, static_cast<int>(sub_steps));
    return true;
}

bool PhysicsWorld::load_map_collisions(const project::MapDocument& map) noexcept {
    if (!valid() || !project::validate_map(project::ProjectManifest{}, map).ok())
        return false;
    if (b2Body_IsValid(impl_->collision_body)) {
        b2DestroyBody(impl_->collision_body);
        impl_->collision_body = b2_nullBodyId;
    }
    auto body_definition = b2DefaultBodyDef();
    body_definition.type = b2_staticBody;
    impl_->collision_body = b2CreateBody(impl_->id, &body_definition);
    if (!b2Body_IsValid(impl_->collision_body)) return false;

    for (const auto& collision : map.collisions) {
        auto shape_definition = b2DefaultShapeDef();
        shape_definition.isSensor = collision.sensor;
        shape_definition.enableSensorEvents = collision.sensor;
        if (collision.kind == project::CollisionShapeKind::circle) {
            const b2Circle circle{{collision.center.x, collision.center.y}, collision.radius};
            if (!b2Shape_IsValid(b2CreateCircleShape(impl_->collision_body,
                                                     &shape_definition, &circle))) return false;
        } else if (collision.kind == project::CollisionShapeKind::capsule) {
            const b2Capsule capsule{
                {collision.center.x - collision.length * 0.5F, collision.center.y},
                {collision.center.x + collision.length * 0.5F, collision.center.y},
                collision.radius};
            if (!b2Shape_IsValid(b2CreateCapsuleShape(impl_->collision_body,
                                                      &shape_definition, &capsule))) return false;
        } else if (collision.kind == project::CollisionShapeKind::polygon) {
            if (collision.points.size() > B2_MAX_POLYGON_VERTICES) return false;
            std::vector<b2Vec2> points;
            points.reserve(collision.points.size());
            for (const auto point : collision.points) points.push_back({point.x, point.y});
            const auto hull = b2ComputeHull(points.data(), static_cast<int>(points.size()));
            if (hull.count < 3) return false;
            const auto polygon = b2MakePolygon(&hull, 0.0F);
            if (!b2Shape_IsValid(b2CreatePolygonShape(impl_->collision_body,
                                                       &shape_definition, &polygon))) return false;
        } else {
            if (collision.sensor || collision.points.size() < 2) return false;
            for (std::size_t index = 1; index < collision.points.size(); ++index) {
                const b2Segment segment{
                    {collision.points[index - 1].x, collision.points[index - 1].y},
                    {collision.points[index].x, collision.points[index].y}};
                if (!b2Shape_IsValid(b2CreateSegmentShape(impl_->collision_body,
                                                           &shape_definition, &segment))) return false;
            }
        }
    }
    return true;
}

bool PhysicsWorld::create_character(const core::Vec2 position,
                                    const core::Vec2 half_extents) noexcept {
    if (!valid() || half_extents.x <= 0.0F || half_extents.y <= 0.0F) return false;
    if (b2Body_IsValid(impl_->character_body)) {
        b2DestroyBody(impl_->character_body);
        impl_->character_body = b2_nullBodyId;
    }
    auto body_definition = b2DefaultBodyDef();
    body_definition.type = b2_dynamicBody;
    body_definition.position = {position.x, position.y};
    body_definition.fixedRotation = true;
    body_definition.enableSleep = false;
    impl_->character_body = b2CreateBody(impl_->id, &body_definition);
    if (!b2Body_IsValid(impl_->character_body)) return false;
    auto shape_definition = b2DefaultShapeDef();
    shape_definition.density = 1.0F;
    shape_definition.material.friction = 0.8F;
    const auto box = b2MakeBox(half_extents.x, half_extents.y);
    if (!b2Shape_IsValid(b2CreatePolygonShape(impl_->character_body,
                                               &shape_definition, &box))) {
        b2DestroyBody(impl_->character_body);
        impl_->character_body = b2_nullBodyId;
        return false;
    }
    return true;
}

void PhysicsWorld::set_character_velocity(const core::Vec2 velocity) noexcept {
    if (character_valid()) b2Body_SetLinearVelocity(impl_->character_body,
                                                     {velocity.x, velocity.y});
}

core::Vec2 PhysicsWorld::character_velocity() const noexcept {
    if (!character_valid()) return {};
    const auto velocity = b2Body_GetLinearVelocity(impl_->character_body);
    return {velocity.x, velocity.y};
}

core::Vec2 PhysicsWorld::character_position() const noexcept {
    if (!character_valid()) return {};
    const auto position = b2Body_GetPosition(impl_->character_body);
    return {position.x, position.y};
}

bool PhysicsWorld::character_valid() const noexcept {
    return valid() && b2Body_IsValid(impl_->character_body);
}

} // namespace fabric::physics
