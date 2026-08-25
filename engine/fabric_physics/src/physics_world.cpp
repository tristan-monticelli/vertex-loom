#include "fabric/physics/physics_world.hpp"

#include <box2d/box2d.h>

#include <new>

namespace fabric::physics {

struct PhysicsWorld::Impl {
    b2WorldId id = b2_nullWorldId;
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

} // namespace fabric::physics
