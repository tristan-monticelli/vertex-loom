#pragma once

#include "fabric/project/animation.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fabric::project {

enum class PropertyValueKind {
    scalar,
    integer,
    text,
    vec2,
    color,
    boolean,
    angle,
    transform,
    resource,
};
enum class PropertyComposition { replace, additive };

struct PropertyDescriptor {
    std::string component_id;
    std::string property_id;
    std::string display_path;
    PropertyValueKind value_kind{PropertyValueKind::scalar};
    bool readable{true};
    bool writable{true};
    bool animatable{true};
    float minimum{};
    float maximum{};
    std::string unit;
    PropertyComposition composition{PropertyComposition::replace};
};

class PropertyDescriptorRegistry {
public:
    [[nodiscard]] ValidationReport register_descriptor(PropertyDescriptor descriptor) {
        ValidationReport result;
        if (descriptor.component_id.empty() || descriptor.property_id.empty() ||
            descriptor.display_path.empty()) {
            result.errors.push_back({ErrorCode::invalid_asset, "property",
                                     "descriptor identifiers and display path are required"});
            return result;
        }
        if (!descriptor.readable && !descriptor.writable) {
            result.errors.push_back({ErrorCode::invalid_asset, "property",
                                     "descriptor must be readable or writable"});
            return result;
        }
        if (descriptor.minimum > descriptor.maximum) {
            result.errors.push_back({ErrorCode::invalid_asset, "range",
                                     "minimum must not exceed maximum"});
            return result;
        }
        for (const auto& existing : descriptors_) {
            if (existing.component_id == descriptor.component_id &&
                existing.property_id == descriptor.property_id) {
                result.errors.push_back({ErrorCode::duplicate_resource, "property",
                                         "property descriptor is already registered"});
                return result;
            }
        }
        descriptors_.push_back(std::move(descriptor));
        return result;
    }

    [[nodiscard]] const PropertyDescriptor* resolve(
        const PropertyBinding& binding) const noexcept {
        for (const auto& descriptor : descriptors_)
            if (descriptor.component_id == binding.component_id &&
                descriptor.property_id == binding.property_id)
                return &descriptor;
        return nullptr;
    }

    [[nodiscard]] std::vector<const PropertyDescriptor*> animatable() const {
        std::vector<const PropertyDescriptor*> result;
        for (const auto& descriptor : descriptors_)
            if (descriptor.animatable && descriptor.writable)
                result.push_back(&descriptor);
        return result;
    }

    [[nodiscard]] const std::vector<PropertyDescriptor>& descriptors() const noexcept {
        return descriptors_;
    }

private:
    std::vector<PropertyDescriptor> descriptors_;
};

} // namespace fabric::project
