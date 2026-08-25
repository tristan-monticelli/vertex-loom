#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/manifest.hpp"

#include <cstddef>
#include <vector>

namespace fabric::project {

struct XpbdParticle {
    core::Vec2 position;
    float inverse_mass{1.0F};
};

struct XpbdDistanceConstraint {
    std::size_t first{};
    std::size_t second{};
    float rest_length{};
    float compliance{};
    float lambda{};
};

struct XpbdPinConstraint {
    std::size_t particle{};
    core::Vec2 target;
    float compliance{};
    core::Vec2 lambda;
};

struct XpbdSystem {
    std::vector<XpbdParticle> particles;
    std::vector<XpbdDistanceConstraint> distance_constraints;
    std::vector<XpbdPinConstraint> pin_constraints;
};

struct XpbdResult {
    std::size_t iterations{};
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] ValidationReport validate_xpbd_system(const XpbdSystem&, float dt,
                                                    std::size_t iterations);
[[nodiscard]] XpbdResult solve_xpbd_substep(XpbdSystem&, float dt,
                                             std::size_t iterations,
                                             float quantization = 4096.0F);

} // namespace fabric::project
