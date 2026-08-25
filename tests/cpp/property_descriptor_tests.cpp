#include "fabric/project/property.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("property descriptors resolve stable animation bindings") {
    fabric::project::PropertyDescriptorRegistry registry;
    REQUIRE(registry.register_descriptor({
        .component_id = "transform", .property_id = "position",
        .display_path = "Transform/Position",
        .value_kind = fabric::project::PropertyValueKind::vec2,
    }).ok());

    const auto* descriptor = registry.resolve({"root", "transform", "position"});
    REQUIRE(descriptor != nullptr);
    REQUIRE(descriptor->value_kind == fabric::project::PropertyValueKind::vec2);
    REQUIRE(registry.animatable().size() == 1);
}

TEST_CASE("property descriptor registry rejects duplicate and invalid descriptors") {
    fabric::project::PropertyDescriptorRegistry registry;
    REQUIRE_FALSE(registry.register_descriptor({}).ok());
    REQUIRE(registry.register_descriptor({"transform", "opacity", "Transform/Opacity"}).ok());
    REQUIRE_FALSE(registry.register_descriptor({"transform", "opacity", "Transform/Opacity"}).ok());
    REQUIRE_FALSE(registry.register_descriptor({
        .component_id = "transform", .property_id = "scale",
        .display_path = "Transform/Scale", .minimum = 2.0F, .maximum = 1.0F,
    }).ok());
}
