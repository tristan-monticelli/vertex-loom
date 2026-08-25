#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/manifest.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fabric::project {

struct FabrikChainDefinition {
    std::string id;
    std::vector<std::string> joints;
    std::string target_node;
    std::size_t max_iterations{16};
    float tolerance{1.0e-3F};
    friend bool operator==(const FabrikChainDefinition&,
                           const FabrikChainDefinition&) = default;
};

struct FabrikRequest {
    std::vector<core::Vec2> joints;
    core::Vec2 target;
    std::size_t max_iterations{16};
    float tolerance{1.0e-3F};
};

struct FabrikResult {
    std::vector<core::Vec2> joints;
    std::size_t iterations{};
    bool converged{};
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] FabrikResult solve_fabrik(const FabrikRequest&);

} // namespace fabric::project
