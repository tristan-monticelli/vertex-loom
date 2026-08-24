#pragma once

#include "fabric/core/resource_id.hpp"

#include <cstdint>
#include <string>

namespace fabric::project {

struct DocumentHeader {
    std::uint32_t schema_version{1};
    std::string type;
    core::ResourceId id;
    std::string name;

    friend bool operator==(const DocumentHeader&, const DocumentHeader&) = default;
};

struct ResourceReference {
    core::ResourceId id;
    std::string expected_type;

    friend bool operator==(const ResourceReference&,
                           const ResourceReference&) = default;
};

} // namespace fabric::project
