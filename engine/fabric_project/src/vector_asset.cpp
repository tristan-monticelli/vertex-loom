#include "fabric/project/vector_asset.hpp"

#include "asset_storage.hpp"
#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool read_string(const Json& object, const char* key, std::string& destination,
                 std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected a JSON string");
        return false;
    }
    destination = iterator->get<std::string>();
    return true;
}

bool read_version(const Json& object, std::uint32_t& destination,
                  std::vector<Error>& errors) {
    const auto iterator = object.find("schemaVersion");
    if (iterator == object.end() || !iterator->is_number_unsigned() ||
        iterator->get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max()) {
        add_error(errors, ErrorCode::invalid_asset, "schemaVersion",
                  "expected an unsigned 32-bit integer");
        return false;
    }
    destination = iterator->get<std::uint32_t>();
    return true;
}

struct Segment {
    core::Vec2 start;
    core::Vec2 end;
};

float cross(const core::Vec2 a, const core::Vec2 b, const core::Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool on_segment(const core::Vec2 point, const core::Vec2 start,
                const core::Vec2 end) {
    constexpr float epsilon = 1.0e-5F;
    return point.x >= std::min(start.x, end.x) - epsilon &&
           point.x <= std::max(start.x, end.x) + epsilon &&
           point.y >= std::min(start.y, end.y) - epsilon &&
           point.y <= std::max(start.y, end.y) + epsilon &&
           std::abs(cross(start, end, point)) <= epsilon;
}

bool segments_intersect(const Segment& first, const Segment& second) {
    constexpr float epsilon = 1.0e-5F;
    const float first_start = cross(first.start, first.end, second.start);
    const float first_end = cross(first.start, first.end, second.end);
    const float second_start = cross(second.start, second.end, first.start);
    const float second_end = cross(second.start, second.end, first.end);
    const auto opposite = [](const float left, const float right) {
        return (left > epsilon && right < -epsilon) ||
               (left < -epsilon && right > epsilon);
    };
    if (opposite(first_start, first_end) &&
        opposite(second_start, second_end)) {
        return true;
    }
    return (std::abs(first_start) <= epsilon &&
            on_segment(second.start, first.start, first.end)) ||
           (std::abs(first_end) <= epsilon &&
            on_segment(second.end, first.start, first.end)) ||
           (std::abs(second_start) <= epsilon &&
            on_segment(first.start, second.start, second.end)) ||
           (std::abs(second_end) <= epsilon &&
            on_segment(first.end, second.start, second.end));
}

std::vector<Segment> path_segments(
    const std::vector<VectorShape::PathCommand>& commands) {
    std::vector<Segment> segments;
    core::Vec2 current{};
    bool has_current = false;
    constexpr int cubic_steps = 32;
    for (const auto& command : commands) {
        if (command.kind == VectorPathCommandKind::move) {
            current = command.point;
            has_current = true;
        } else if (command.kind == VectorPathCommandKind::line &&
                   has_current) {
            segments.push_back({current, command.point});
            current = command.point;
        } else if (command.kind == VectorPathCommandKind::cubic &&
                   has_current) {
            const auto start = current;
            for (int index = 1; index <= cubic_steps; ++index) {
                const float t = static_cast<float>(index) /
                                static_cast<float>(cubic_steps);
                const float inverse = 1.0F - t;
                const core::Vec2 next{
                    inverse * inverse * inverse * start.x +
                        3.0F * inverse * inverse * t * command.control1.x +
                        3.0F * inverse * t * t * command.control2.x +
                        t * t * t * command.point.x,
                    inverse * inverse * inverse * start.y +
                        3.0F * inverse * inverse * t * command.control1.y +
                        3.0F * inverse * t * t * command.control2.y +
                        t * t * t * command.point.y};
                segments.push_back({current, next});
                current = next;
            }
        } else if (command.kind == VectorPathCommandKind::close &&
                   has_current && !segments.empty()) {
            const auto first = segments.front().start;
            if (!(current == first)) segments.push_back({current, first});
            current = first;
        }
    }
    return segments;
}

bool has_self_intersection(const VectorShape& shape) {
    if (shape.kind != VectorShapeKind::path) return false;
    const auto segments = path_segments(shape.path);
    for (std::size_t first = 0; first < segments.size(); ++first) {
        for (std::size_t second = first + 1U;
             second < segments.size(); ++second) {
            if (second == first + 1U ||
                (first == 0U && second + 1U == segments.size() &&
                 segments[first].start == segments[second].end)) {
                continue;
            }
            if (segments_intersect(segments[first], segments[second])) {
                return true;
            }
        }
    }
    return false;
}

bool read_source_kind(const Json& object, VectorSourceKind& destination,
                      std::vector<Error>& errors) {
    std::string value;
    if (!read_string(object, "sourceKind", value, errors)) {
        return false;
    }
    if (value == "linkedSvg") {
        destination = VectorSourceKind::linked_svg;
        return true;
    }
    if (value == "native") {
        destination = VectorSourceKind::native;
        return true;
    }
    add_error(errors, ErrorCode::invalid_asset, "sourceKind",
              "must be linkedSvg or native");
    return false;
}

bool read_bool(const Json& object, const char* key, bool& destination,
               std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_boolean()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a JSON boolean");
        return false;
    }
    destination = iterator->get<bool>();
    return true;
}

bool read_float(const Json& object, const char* key, float& destination,
                std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a finite number");
        return false;
    }
    destination = iterator->get<float>();
    if (!std::isfinite(destination)) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a finite number");
        return false;
    }
    return true;
}

bool read_vec2(const Json& object, const char* key, core::Vec2& destination,
               std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected an object with x and y");
        return false;
    }
    const bool x_ok = read_float(*iterator, "x", destination.x, errors,
                                 field + ".x");
    const bool y_ok = read_float(*iterator, "y", destination.y, errors,
                                 field + ".y");
    return x_ok && y_ok;
}

bool read_point(const Json& object, core::Vec2& destination,
                std::vector<Error>& errors, const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected an object with x and y");
        return false;
    }
    const bool x_ok = read_float(object, "x", destination.x, errors,
                                 field + ".x");
    const bool y_ok = read_float(object, "y", destination.y, errors,
                                 field + ".y");
    return x_ok && y_ok;
}

Json serialize_vec2(const core::Vec2& value) {
    return {{"x", value.x}, {"y", value.y}};
}

Json serialize_transform(const core::Transform& transform) {
    return {
        {"position", serialize_vec2(transform.position)},
        {"rotationDegrees", transform.rotation_degrees},
        {"scale", serialize_vec2(transform.scale)},
        {"pivot", serialize_vec2(transform.pivot)},
    };
}

bool read_transform(const Json& object, core::Transform& destination,
                    std::vector<Error>& errors, const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a transform object");
        return false;
    }
    const bool position_ok = read_vec2(
        object, "position", destination.position, errors, field + ".position");
    const bool rotation_ok = read_float(object, "rotationDegrees",
                                        destination.rotation_degrees, errors,
                                        field + ".rotationDegrees");
    const bool scale_ok = read_vec2(
        object, "scale", destination.scale, errors, field + ".scale");
    const bool pivot_ok = read_vec2(
        object, "pivot", destination.pivot, errors, field + ".pivot");
    return position_ok && rotation_ok && scale_ok && pivot_ok;
}

bool read_origin(const Json& object, VectorOrigin& destination,
                 std::vector<Error>& errors) {
    std::string value;
    if (!read_string(object, "origin", value, errors)) {
        return false;
    }
    if (value == "center") {
        destination = VectorOrigin::center;
        return true;
    }
    if (value == "topLeft") {
        destination = VectorOrigin::top_left;
        return true;
    }
    add_error(errors, ErrorCode::invalid_asset, "native.origin",
              "must be center or topLeft");
    return false;
}

bool read_shape_kind(const Json& object, VectorShapeKind& destination,
                     std::vector<Error>& errors, const std::string& field) {
    std::string value;
    if (!read_string(object, "kind", value, errors)) {
        return false;
    }
    if (value == "rectangle") {
        destination = VectorShapeKind::rectangle;
        return true;
    }
    if (value == "ellipse") {
        destination = VectorShapeKind::ellipse;
        return true;
    }
    if (value == "line") {
        destination = VectorShapeKind::line;
        return true;
    }
    if (value == "path") {
        destination = VectorShapeKind::path;
        return true;
    }
    add_error(errors, ErrorCode::invalid_asset, field,
              "must be rectangle, ellipse, line or path");
    return false;
}

bool read_path_command_kind(const Json& object,
                            VectorPathCommandKind& destination,
                            std::vector<Error>& errors,
                            const std::string& field) {
    std::string value;
    if (!read_string(object, "kind", value, errors)) {
        return false;
    }
    if (value == "move") destination = VectorPathCommandKind::move;
    else if (value == "line") destination = VectorPathCommandKind::line;
    else if (value == "cubic") destination = VectorPathCommandKind::cubic;
    else if (value == "close") destination = VectorPathCommandKind::close;
    else {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "must be move, line, cubic or close");
        return false;
    }
    return true;
}

bool read_fill_kind(const Json& object, VectorFillKind& destination,
                    std::vector<Error>& errors, const std::string& field) {
    std::string value;
    if (!read_string(object, "kind", value, errors)) {
        return false;
    }
    if (value == "solid") {
        destination = VectorFillKind::solid;
        return true;
    }
    if (value == "image") {
        destination = VectorFillKind::image;
        return true;
    }
    if (value == "none") {
        destination = VectorFillKind::none;
        return true;
    }
    add_error(errors, ErrorCode::invalid_asset, field,
              "must be solid, image or none");
    return false;
}

bool read_color(const Json& object, core::Color& destination,
                std::vector<Error>& errors, const std::string& field);

bool read_stroke_join(const Json& object, VectorStrokeJoin& destination,
                      std::vector<Error>& errors, const std::string& field) {
    std::string value;
    if (!read_string(object, "join", value, errors)) return false;
    if (value == "miter") destination = VectorStrokeJoin::miter;
    else if (value == "round") destination = VectorStrokeJoin::round;
    else if (value == "bevel") destination = VectorStrokeJoin::bevel;
    else {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "must be miter, round or bevel");
        return false;
    }
    return true;
}

bool read_stroke_cap(const Json& object, VectorStrokeCap& destination,
                     std::vector<Error>& errors, const std::string& field) {
    std::string value;
    if (!read_string(object, "cap", value, errors)) return false;
    if (value == "butt") destination = VectorStrokeCap::butt;
    else if (value == "round") destination = VectorStrokeCap::round;
    else if (value == "square") destination = VectorStrokeCap::square;
    else {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "must be butt, round or square");
        return false;
    }
    return true;
}

std::optional<VectorStroke> read_stroke(const Json& object,
                                        std::vector<Error>& errors,
                                        const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a stroke object");
        return std::nullopt;
    }
    VectorStroke stroke;
    const auto color = object.find("color");
    if (color == object.end() ||
        !read_color(*color, stroke.color, errors, field + ".color")) {
        return std::nullopt;
    }
    read_float(object, "width", stroke.width, errors, field + ".width");
    read_stroke_join(object, stroke.join, errors, field + ".join");
    read_stroke_cap(object, stroke.cap, errors, field + ".cap");
    return stroke;
}

bool read_image_fit(const Json& object, VectorImageFit& destination,
                    std::vector<Error>& errors, const std::string& field) {
    std::string value;
    if (!read_string(object, "fit", value, errors)) {
        return false;
    }
    if (value == "contain") destination = VectorImageFit::contain;
    else if (value == "cover") destination = VectorImageFit::cover;
    else if (value == "stretch") destination = VectorImageFit::stretch;
    else if (value == "free") destination = VectorImageFit::free;
    else {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "must be contain, cover, stretch or free");
        return false;
    }
    return true;
}

std::optional<VectorImageFill> read_image_fill(
    const Json& fill, std::vector<Error>& errors, const std::string& field) {
    const auto image_iterator = fill.find("image");
    if (image_iterator == fill.end() || !image_iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "image fills require an image object");
        return std::nullopt;
    }
    VectorImageFill image;
    const auto texture = image_iterator->find("texture");
    if (texture == image_iterator->end() || !texture->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field + ".texture",
                  "expected a resource reference");
    } else {
        read_string(*texture, "id", image.texture.id.value, errors);
        read_string(*texture, "expectedType", image.texture.expected_type,
                    errors);
    }
    read_image_fit(*image_iterator, image.fit, errors, field + ".fit");
    const auto transform = image_iterator->find("transform");
    if (transform == image_iterator->end()) {
        add_error(errors, ErrorCode::invalid_asset, field + ".transform",
                  "expected a transform object");
    } else {
        read_transform(*transform, image.transform, errors,
                       field + ".transform");
    }
    read_float(*image_iterator, "opacity", image.opacity, errors,
               field + ".opacity");
    read_bool(*image_iterator, "deformWithShape", image.deform_with_shape,
              errors, field + ".deformWithShape");
    return image;
}

bool read_color(const Json& object, core::Color& destination,
                std::vector<Error>& errors, const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a color object");
        return false;
    }
    const bool red_ok = read_float(
        object, "red", destination.red, errors, field + ".red");
    const bool green_ok = read_float(
        object, "green", destination.green, errors, field + ".green");
    const bool blue_ok = read_float(
        object, "blue", destination.blue, errors, field + ".blue");
    const bool alpha_ok = read_float(
        object, "alpha", destination.alpha, errors, field + ".alpha");
    return red_ok && green_ok && blue_ok && alpha_ok;
}

std::optional<NativeVectorDefinition> read_native(
    const Json& document, std::vector<Error>& errors) {
    const auto native_iterator = document.find("native");
    if (native_iterator == document.end() || !native_iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, "native",
                  "native vectors require a geometry object");
        return std::nullopt;
    }
    NativeVectorDefinition native;
    read_vec2(*native_iterator, "size", native.size, errors, "native.size");
    read_origin(*native_iterator, native.origin, errors);
    const auto nodes_iterator = native_iterator->find("nodes");
    if (nodes_iterator == native_iterator->end() ||
        !nodes_iterator->is_array()) {
        add_error(errors, ErrorCode::invalid_asset, "native.nodes",
                  "expected an array");
        return std::nullopt;
    }
    for (std::size_t index = 0; index < nodes_iterator->size(); ++index) {
        const Json& node_json = (*nodes_iterator)[index];
        const std::string prefix = "native.nodes[" + std::to_string(index) + "]";
        if (!node_json.is_object()) {
            add_error(errors, ErrorCode::invalid_asset, prefix,
                      "expected a node object");
            continue;
        }
        VectorNode node;
        read_string(node_json, "id", node.id, errors);
        read_string(node_json, "name", node.name, errors);
        read_bool(node_json, "visible", node.visible, errors,
                  prefix + ".visible");
        read_bool(node_json, "locked", node.locked, errors,
                  prefix + ".locked");
        const auto transform = node_json.find("transform");
        if (transform == node_json.end()) {
            add_error(errors, ErrorCode::invalid_asset, prefix + ".transform",
                      "expected a transform object");
        } else {
            read_transform(*transform, node.transform, errors,
                           prefix + ".transform");
        }
        const auto shape = node_json.find("shape");
        if (shape == node_json.end() || !shape->is_object()) {
            add_error(errors, ErrorCode::invalid_asset, prefix + ".shape",
                      "expected a shape object");
        } else {
            read_string(*shape, "id", node.shape.id, errors);
            read_shape_kind(*shape, node.shape.kind, errors,
                            prefix + ".shape.kind");
            const auto bounds = shape->find("bounds");
            if (bounds == shape->end() || !bounds->is_object()) {
                add_error(errors, ErrorCode::invalid_asset,
                          prefix + ".shape.bounds", "expected a rect object");
            } else {
                read_vec2(*bounds, "origin", node.shape.bounds.origin, errors,
                          prefix + ".shape.bounds.origin");
                read_vec2(*bounds, "size", node.shape.bounds.size, errors,
                          prefix + ".shape.bounds.size");
            }
            if (node.shape.kind == VectorShapeKind::line) {
                const auto points = shape->find("points");
                if (points == shape->end() || !points->is_array()) {
                    add_error(errors, ErrorCode::invalid_asset,
                              prefix + ".shape.points",
                              "line shapes require exactly two points");
                } else {
                    for (std::size_t point_index = 0;
                         point_index < points->size(); ++point_index) {
                        core::Vec2 point;
                        if (read_point((*points)[point_index], point, errors,
                                       prefix + ".shape.points[" +
                                           std::to_string(point_index) + "]")) {
                            node.shape.points.push_back(point);
                        }
                    }
                }
            }
            if (node.shape.kind == VectorShapeKind::path) {
                const auto path = shape->find("path");
                if (path == shape->end() || !path->is_array()) {
                    add_error(errors, ErrorCode::invalid_asset,
                              prefix + ".shape.path",
                              "path shapes require a command array");
                } else {
                    for (std::size_t command_index = 0;
                         command_index < path->size(); ++command_index) {
                        const auto& command_json = (*path)[command_index];
                        const std::string command_field =
                            prefix + ".shape.path[" +
                            std::to_string(command_index) + "]";
                        VectorShape::PathCommand command;
                        if (!command_json.is_object() ||
                            !read_path_command_kind(command_json, command.kind,
                                                    errors, command_field + ".kind")) {
                            continue;
                        }
                        if (command.kind != VectorPathCommandKind::close) {
                            const auto point = command_json.find("point");
                            if (point == command_json.end() ||
                                !read_point(*point, command.point, errors,
                                            command_field + ".point")) {
                                continue;
                            }
                        }
                        if (command.kind == VectorPathCommandKind::cubic) {
                            const auto control1 = command_json.find("control1");
                            const auto control2 = command_json.find("control2");
                            if (control1 == command_json.end() ||
                                control2 == command_json.end() ||
                                !read_point(*control1, command.control1, errors,
                                             command_field + ".control1") ||
                                !read_point(*control2, command.control2, errors,
                                             command_field + ".control2")) {
                                continue;
                            }
                        }
                        node.shape.path.push_back(command);
                    }
                }
            }
        }
        const auto fill = node_json.find("fill");
        if (fill == node_json.end() || !fill->is_object()) {
            add_error(errors, ErrorCode::invalid_asset, prefix + ".fill",
                      "expected a fill object");
        } else if (read_fill_kind(*fill, node.fill.kind, errors,
                                  prefix + ".fill.kind")) {
            if (node.fill.kind == VectorFillKind::solid) {
                if (fill->contains("image")) {
                    add_error(errors, ErrorCode::invalid_asset,
                              prefix + ".fill.image",
                              "solid fills must not declare image data");
                }
                const auto color = fill->find("color");
                if (color == fill->end()) {
                    add_error(errors, ErrorCode::invalid_asset,
                              prefix + ".fill.color", "expected a color object");
                } else {
                    core::Color parsed_color;
                    if (read_color(*color, parsed_color, errors,
                                   prefix + ".fill.color")) {
                        node.fill.color = parsed_color;
                    }
                }
            } else if (node.fill.kind == VectorFillKind::image) {
                if (fill->contains("color")) {
                    add_error(errors, ErrorCode::invalid_asset,
                              prefix + ".fill.color",
                              "image fills must not declare a color");
                }
                node.fill.image = read_image_fill(
                    *fill, errors, prefix + ".fill.image");
            } else if (fill->contains("color") || fill->contains("image")) {
                add_error(errors, ErrorCode::invalid_asset, prefix + ".fill",
                          "none fills must not declare color or image data");
            }
        }
        const auto stroke = node_json.find("stroke");
        if (stroke != node_json.end()) {
            node.stroke = read_stroke(*stroke, errors, prefix + ".stroke");
        }
        if (const auto parent = node_json.find("parent");
            parent != node_json.end()) {
            std::string parent_id;
            if (read_string(node_json, "parent", parent_id, errors)) {
                node.parent_id = std::move(parent_id);
            }
        }
        if (const auto clip = node_json.find("clip");
            clip != node_json.end()) {
            std::string clip_id;
            if (read_string(node_json, "clip", clip_id, errors)) {
                node.clip_node_id = std::move(clip_id);
            }
        }
        native.nodes.push_back(std::move(node));
    }
    return native;
}

} // namespace

std::string_view to_string(const VectorSourceKind kind) noexcept {
    switch (kind) {
    case VectorSourceKind::linked_svg: return "linkedSvg";
    case VectorSourceKind::native: return "native";
    }
    return "native";
}

std::string_view to_string(const VectorOrigin origin) noexcept {
    switch (origin) {
    case VectorOrigin::center: return "center";
    case VectorOrigin::top_left: return "topLeft";
    }
    return "center";
}

std::string_view to_string(const VectorShapeKind kind) noexcept {
    switch (kind) {
    case VectorShapeKind::rectangle: return "rectangle";
    case VectorShapeKind::ellipse: return "ellipse";
    case VectorShapeKind::line: return "line";
    case VectorShapeKind::path: return "path";
    }
    return "rectangle";
}

std::string_view to_string(const VectorPathCommandKind kind) noexcept {
    switch (kind) {
    case VectorPathCommandKind::move: return "move";
    case VectorPathCommandKind::line: return "line";
    case VectorPathCommandKind::cubic: return "cubic";
    case VectorPathCommandKind::close: return "close";
    }
    return "move";
}

std::string_view to_string(const VectorFillKind kind) noexcept {
    switch (kind) {
    case VectorFillKind::solid: return "solid";
    case VectorFillKind::image: return "image";
    case VectorFillKind::none: return "none";
    }
    return "none";
}

std::string_view to_string(const VectorStrokeJoin join) noexcept {
    switch (join) {
    case VectorStrokeJoin::miter: return "miter";
    case VectorStrokeJoin::round: return "round";
    case VectorStrokeJoin::bevel: return "bevel";
    }
    return "miter";
}

std::string_view to_string(const VectorStrokeCap cap) noexcept {
    switch (cap) {
    case VectorStrokeCap::butt: return "butt";
    case VectorStrokeCap::round: return "round";
    case VectorStrokeCap::square: return "square";
    }
    return "butt";
}

std::string_view to_string(const VectorImageFit fit) noexcept {
    switch (fit) {
    case VectorImageFit::contain: return "contain";
    case VectorImageFit::cover: return "cover";
    case VectorImageFit::stretch: return "stretch";
    case VectorImageFit::free: return "free";
    }
    return "cover";
}

std::filesystem::path vector_source_path(const ProjectManifest& manifest,
                                         const core::ResourceId& id) {
    return manifest.directories.assets / "vectors" / (id.value + ".svg");
}

std::filesystem::path vector_document_path(const ProjectManifest& manifest,
                                           const core::ResourceId& id) {
    return manifest.directories.assets / "vectors" /
           (id.value + ".vector.json");
}

ValidationReport validate_vector_asset(const ProjectManifest& manifest,
                                       const VectorAsset& asset) {
    ValidationReport report;
    if (asset.document.schema_version != current_vector_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only vector schema version 2 is supported");
    }
    if (asset.document.type != "vector") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be vector");
    }
    if (!core::ResourceId::is_valid(asset.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (asset.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (asset.source_kind == VectorSourceKind::linked_svg) {
        if (!detail::is_portable_relative_path(asset.source) ||
            asset.source != vector_source_path(manifest, asset.document.id)) {
            add_error(report.errors, ErrorCode::invalid_path, "source",
                      "linkedSvg source must use the canonical project-relative vector path");
        }
        if (asset.native.has_value()) {
            add_error(report.errors, ErrorCode::invalid_asset, "native",
                      "linkedSvg vectors must not contain native geometry");
        }
    } else {
        if (!asset.source.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, "source",
                      "native vectors must not declare an SVG source");
        }
        if (!asset.native.has_value()) {
            add_error(report.errors, ErrorCode::invalid_asset, "native",
                      "native vectors require geometry");
            return report;
        }
        const auto finite = [](const float value) {
            return std::isfinite(value);
        };
        const auto positive_size = [&finite](const core::Vec2& size) {
            return finite(size.x) && finite(size.y) && size.x > 0.0F &&
                   size.y > 0.0F && size.x <= 1'000'000.0F &&
                   size.y <= 1'000'000.0F;
        };
        if (!positive_size(asset.native->size)) {
            add_error(report.errors, ErrorCode::invalid_asset, "native.size",
                      "must be finite, positive and at most 1,000,000 world units");
        }
        std::set<std::string> node_ids;
        std::set<std::string> shape_ids;
        for (std::size_t index = 0; index < asset.native->nodes.size(); ++index) {
            const auto& node = asset.native->nodes[index];
            const std::string prefix =
                "native.nodes[" + std::to_string(index) + "]";
            if (!core::ResourceId::is_valid(node.id) ||
                !node_ids.insert(node.id).second) {
                add_error(report.errors, ErrorCode::invalid_resource_id,
                          prefix + ".id", "must be a unique stable identifier");
            }
            if (node.name.empty()) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          prefix + ".name", "must not be empty");
            }
            const auto& transform = node.transform;
            if (!finite(transform.position.x) ||
                !finite(transform.position.y) ||
                !finite(transform.rotation_degrees) ||
                !finite(transform.scale.x) || !finite(transform.scale.y) ||
                transform.scale.x == 0.0F || transform.scale.y == 0.0F ||
                !finite(transform.pivot.x) || !finite(transform.pivot.y)) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          prefix + ".transform",
                          "must contain finite values and non-zero scale");
            }
            if (!core::ResourceId::is_valid(node.shape.id) ||
                !shape_ids.insert(node.shape.id).second) {
                add_error(report.errors, ErrorCode::invalid_resource_id,
                          prefix + ".shape.id",
                          "must be a unique stable identifier");
            }
            if (!finite(node.shape.bounds.origin.x) ||
                !finite(node.shape.bounds.origin.y) ||
                !positive_size(node.shape.bounds.size)) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          prefix + ".shape.bounds",
                          "must contain a finite origin and positive bounded size");
            }
            if (node.shape.kind == VectorShapeKind::line) {
                if (node.shape.points.size() != 2U) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".shape.points",
                              "line shapes require exactly two points");
                } else {
                    const auto& first = node.shape.points[0];
                    const auto& second = node.shape.points[1];
                    if (!finite(first.x) || !finite(first.y) ||
                        !finite(second.x) || !finite(second.y) ||
                        (first.x == second.x && first.y == second.y)) {
                        add_error(report.errors, ErrorCode::invalid_asset,
                                  prefix + ".shape.points",
                                  "line endpoints must be finite and distinct");
                    }
                }
            } else if (node.shape.kind == VectorShapeKind::path) {
                const auto& commands = node.shape.path;
                bool valid_sequence = commands.size() >= 2U &&
                    commands.front().kind == VectorPathCommandKind::move;
                std::size_t segments = 0;
                bool closed = false;
                for (std::size_t command_index = 0;
                     command_index < commands.size(); ++command_index) {
                    const auto& command = commands[command_index];
                    if (command.kind != VectorPathCommandKind::close &&
                        (!finite(command.point.x) || !finite(command.point.y))) {
                        valid_sequence = false;
                    }
                    if (command.kind == VectorPathCommandKind::cubic &&
                        (!finite(command.control1.x) ||
                         !finite(command.control1.y) ||
                         !finite(command.control2.x) ||
                         !finite(command.control2.y))) {
                        valid_sequence = false;
                    }
                    if (command.kind != VectorPathCommandKind::close) {
                        ++segments;
                    } else if (closed || command_index + 1U != commands.size()) {
                        valid_sequence = false;
                    } else {
                        closed = true;
                    }
                }
                if (segments < 2U) {
                    valid_sequence = false;
                }
                if (!valid_sequence) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".shape.path",
                              "must start with move, contain a segment and end close if present");
                } else if (has_self_intersection(node.shape)) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".shape.path",
                              "must not contain self-intersections");
                }
            } else if (!node.shape.points.empty()) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          prefix + ".shape.points",
                          "only line shapes may declare points");
            }
            if (node.shape.kind != VectorShapeKind::path &&
                !node.shape.path.empty()) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          prefix + ".shape.path",
                          "only path shapes may declare commands");
            }
            if (node.stroke.has_value()) {
                const auto& stroke = *node.stroke;
                const auto valid_channel = [&finite](const float channel) {
                    return finite(channel) && channel >= 0.0F &&
                           channel <= 1.0F;
                };
                if (!valid_channel(stroke.color.red) ||
                    !valid_channel(stroke.color.green) ||
                    !valid_channel(stroke.color.blue) ||
                    !valid_channel(stroke.color.alpha)) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".stroke.color",
                              "channels must be finite values from 0 to 1");
                }
                if (!finite(stroke.width) || stroke.width <= 0.0F ||
                    stroke.width > 100'000.0F) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".stroke.width",
                              "must be finite, positive and bounded");
                }
            }
            if (node.fill.kind == VectorFillKind::solid) {
                if (!node.fill.color.has_value()) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".fill.color",
                              "solid fills require a color");
                    continue;
                }
                const auto& color = *node.fill.color;
                const auto valid_channel = [&finite](const float channel) {
                    return finite(channel) && channel >= 0.0F &&
                           channel <= 1.0F;
                };
                if (!valid_channel(color.red) || !valid_channel(color.green) ||
                    !valid_channel(color.blue) || !valid_channel(color.alpha)) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".fill.color",
                              "channels must be finite values from 0 to 1");
                }
                if (node.fill.image.has_value()) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".fill.image",
                              "solid fills must not contain image data");
                }
            } else if (node.fill.kind == VectorFillKind::image) {
                if (node.fill.color.has_value() || !node.fill.image.has_value()) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".fill",
                              "image fills require image data and no color");
                    continue;
                }
                const auto& image = *node.fill.image;
                if (!core::ResourceId::is_valid(image.texture.id.value)) {
                    add_error(report.errors, ErrorCode::invalid_resource_id,
                              prefix + ".fill.image.texture.id",
                              "must be a valid texture identifier");
                }
                if (image.texture.expected_type != "texture") {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".fill.image.texture.expectedType",
                              "must be texture");
                }
                const auto& fill_transform = image.transform;
                if (!finite(fill_transform.position.x) ||
                    !finite(fill_transform.position.y) ||
                    !finite(fill_transform.rotation_degrees) ||
                    !finite(fill_transform.scale.x) ||
                    !finite(fill_transform.scale.y) ||
                    fill_transform.scale.x == 0.0F ||
                    fill_transform.scale.y == 0.0F ||
                    !finite(fill_transform.pivot.x) ||
                    !finite(fill_transform.pivot.y)) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".fill.image.transform",
                              "must contain finite values and non-zero scale");
                }
                if (!finite(image.opacity) || image.opacity < 0.0F ||
                    image.opacity > 1.0F) {
                    add_error(report.errors, ErrorCode::invalid_asset,
                              prefix + ".fill.image.opacity",
                              "must be a finite value from 0 to 1");
                }
            } else if (node.fill.color.has_value() ||
                       node.fill.image.has_value()) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          prefix + ".fill",
                          "none fills must not contain color or image data");
            }
        }
        const auto& nodes = asset.native->nodes;
        const auto find_node = [&nodes](const std::string& id)
            -> const VectorNode* {
            const auto iterator = std::ranges::find_if(
                nodes, [&id](const VectorNode& candidate) {
                    return candidate.id == id;
                });
            return iterator == nodes.end() ? nullptr : &*iterator;
        };
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            const auto& node = nodes[index];
            const std::string prefix =
                "native.nodes[" + std::to_string(index) + "]";
            if (node.parent_id.has_value()) {
                if (!core::ResourceId::is_valid(*node.parent_id) ||
                    find_node(*node.parent_id) == nullptr ||
                    *node.parent_id == node.id) {
                    add_error(report.errors, ErrorCode::invalid_resource_id,
                              prefix + ".parent",
                              "must reference an existing different node");
                } else {
                    std::set<std::string> visited;
                    const VectorNode* current = &node;
                    while (current->parent_id.has_value()) {
                        if (!visited.insert(current->id).second) {
                            add_error(report.errors, ErrorCode::invalid_asset,
                                      prefix + ".parent",
                                      "parent references must not form a cycle");
                            break;
                        }
                        current = find_node(*current->parent_id);
                        if (current == nullptr) break;
                    }
                }
            }
            if (node.clip_node_id.has_value() &&
                (!core::ResourceId::is_valid(*node.clip_node_id) ||
                 find_node(*node.clip_node_id) == nullptr ||
                 *node.clip_node_id == node.id)) {
                add_error(report.errors, ErrorCode::invalid_resource_id,
                          prefix + ".clip",
                          "must reference an existing different node");
            }
        }
    }
    return report;
}

std::vector<ResourceReference> vector_resource_references(
    const VectorAsset& asset) {
    std::vector<ResourceReference> references;
    if (!asset.native.has_value()) {
        return references;
    }
    for (const auto& node : asset.native->nodes) {
        if (node.fill.kind == VectorFillKind::image &&
            node.fill.image.has_value()) {
            references.push_back(node.fill.image->texture);
        }
    }
    return references;
}

std::string serialize_vector_asset(const VectorAsset& asset) {
    Json document = {
        {"schemaVersion", asset.document.schema_version},
        {"type", asset.document.type},
        {"id", asset.document.id.value},
        {"name", asset.document.name},
        {"sourceKind", std::string(to_string(asset.source_kind))},
    };
    if (asset.source_kind == VectorSourceKind::linked_svg) {
        document["source"] = asset.source.generic_string();
    } else if (asset.native.has_value()) {
        Json nodes = Json::array();
        for (const auto& node : asset.native->nodes) {
            Json fill = {{"kind", std::string(to_string(node.fill.kind))}};
            if (node.fill.kind == VectorFillKind::solid &&
                node.fill.color.has_value()) {
                fill["color"] = {
                    {"red", node.fill.color->red},
                    {"green", node.fill.color->green},
                    {"blue", node.fill.color->blue},
                    {"alpha", node.fill.color->alpha},
                };
            } else if (node.fill.kind == VectorFillKind::image &&
                       node.fill.image.has_value()) {
                const auto& image = *node.fill.image;
                fill["image"] = {
                    {"texture",
                     {{"id", image.texture.id.value},
                      {"expectedType", image.texture.expected_type}}},
                    {"fit", std::string(to_string(image.fit))},
                    {"transform", serialize_transform(image.transform)},
                    {"opacity", image.opacity},
                    {"deformWithShape", image.deform_with_shape},
                };
            }
            Json shape = {
                {"id", node.shape.id},
                {"kind", std::string(to_string(node.shape.kind))},
                {"bounds",
                 {{"origin", serialize_vec2(node.shape.bounds.origin)},
                  {"size", serialize_vec2(node.shape.bounds.size)}}},
            };
            if (node.shape.kind == VectorShapeKind::line) {
                shape["points"] = Json::array();
                for (const auto& point : node.shape.points) {
                    shape["points"].push_back(serialize_vec2(point));
                }
            }
            if (node.shape.kind == VectorShapeKind::path) {
                shape["path"] = Json::array();
                for (const auto& command : node.shape.path) {
                    Json command_json = {
                        {"kind", std::string(to_string(command.kind))},
                    };
                    if (command.kind != VectorPathCommandKind::close) {
                        command_json["point"] = serialize_vec2(command.point);
                    }
                    if (command.kind == VectorPathCommandKind::cubic) {
                        command_json["control1"] =
                            serialize_vec2(command.control1);
                        command_json["control2"] =
                            serialize_vec2(command.control2);
                    }
                    shape["path"].push_back(std::move(command_json));
                }
            }
            Json node_json = {
                {"id", node.id},
                {"name", node.name},
                {"visible", node.visible},
                {"locked", node.locked},
                {"transform", serialize_transform(node.transform)},
                {"shape", std::move(shape)},
                {"fill", std::move(fill)},
            };
            if (node.stroke.has_value()) {
                const auto& stroke = *node.stroke;
                node_json["stroke"] = {
                    {"color",
                     {{"red", stroke.color.red},
                      {"green", stroke.color.green},
                      {"blue", stroke.color.blue},
                      {"alpha", stroke.color.alpha}}},
                    {"width", stroke.width},
                    {"join", std::string(to_string(stroke.join))},
                    {"cap", std::string(to_string(stroke.cap))},
                };
            }
            if (node.parent_id.has_value()) {
                node_json["parent"] = *node.parent_id;
            }
            if (node.clip_node_id.has_value()) {
                node_json["clip"] = *node.clip_node_id;
            }
            nodes.push_back(std::move(node_json));
        }
        document["native"] = {
            {"size", serialize_vec2(asset.native->size)},
            {"origin", std::string(to_string(asset.native->origin))},
            {"nodes", std::move(nodes)},
        };
    }
    return document.dump(2) + '\n';
}

VectorAssetResult parse_vector_asset(const ProjectManifest& manifest,
                                     const std::string_view json_text) {
    VectorAssetResult result;
    Json document;
    try {
        document = Json::parse(json_text);
    } catch (const Json::parse_error&) {
        add_error(result.errors, ErrorCode::invalid_json, "vector",
                  "cannot parse vector asset JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_asset, "vector",
                  "top-level value must be an object");
        return result;
    }

    VectorAsset asset;
    std::uint32_t source_version{};
    read_version(document, source_version, result.errors);
    read_string(document, "type", asset.document.type, result.errors);
    read_string(document, "id", asset.document.id.value, result.errors);
    read_string(document, "name", asset.document.name, result.errors);
    if (source_version == 1) {
        std::string format;
        read_string(document, "format", format, result.errors);
        if (format != "svg") {
            add_error(result.errors, ErrorCode::invalid_asset, "format",
                      "vector schema version 1 only supports svg");
        }
        asset.source_kind = VectorSourceKind::linked_svg;
    } else if (source_version == current_vector_schema_version) {
        read_source_kind(document, asset.source_kind, result.errors);
    } else if (result.errors.empty()) {
        add_error(result.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only vector schema versions 1 and 2 are readable");
    }
    if (asset.source_kind == VectorSourceKind::linked_svg) {
        if (source_version == current_vector_schema_version &&
            document.contains("native")) {
            add_error(result.errors, ErrorCode::invalid_asset, "native",
                      "linkedSvg vectors must not declare native geometry");
        }
        std::string source;
        if (read_string(document, "source", source, result.errors)) {
            asset.source = source;
        }
    } else if (source_version == current_vector_schema_version) {
        if (document.contains("source")) {
            add_error(result.errors, ErrorCode::invalid_asset, "source",
                      "native vectors must not declare an SVG source");
        }
        asset.native = read_native(document, result.errors);
    }
    asset.document.schema_version = current_vector_schema_version;
    if (!result.errors.empty()) {
        return result;
    }
    auto validation = validate_vector_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(asset);
    return result;
}

VectorAssetResult load_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path) {
    VectorAssetResult result;
    if (!detail::is_portable_relative_path(document_path)) {
        add_error(result.errors, ErrorCode::invalid_path, "vector",
                  "document path must be project-relative");
        return result;
    }
    std::error_code filesystem_error;
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return result;
    }
    filesystem_error.clear();
    const auto canonical_document = std::filesystem::weakly_canonical(
        project_root / document_path, filesystem_error);
    if (filesystem_error ||
        !detail::is_within(canonical_root, canonical_document)) {
        add_error(result.errors, ErrorCode::invalid_path, "vector",
                  "vector document must resolve inside the project");
        return result;
    }
    std::ifstream input(canonical_document, std::ios::binary);
    if (!input) {
        add_error(result.errors, ErrorCode::missing_file, "vector",
                  "cannot open vector asset document");
        return result;
    }
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    result = parse_vector_asset(manifest, contents);
    if (!result.ok()) {
        return result;
    }
    if (document_path != vector_document_path(manifest,
                                              result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "vector",
                  "document filename does not match its resource identifier");
        return result;
    }

    if (result.asset->source_kind == VectorSourceKind::linked_svg) {
        filesystem_error.clear();
        const auto canonical_source = std::filesystem::weakly_canonical(
            project_root / result.asset->source, filesystem_error);
        const bool source_is_file = !filesystem_error &&
            std::filesystem::is_regular_file(canonical_source, filesystem_error);
        if (!filesystem_error && source_is_file &&
            !detail::is_within(canonical_root, canonical_source)) {
            result.asset.reset();
            add_error(result.errors, ErrorCode::invalid_path, "source",
                      "vector source must resolve inside the project");
        } else if (filesystem_error || !source_is_file) {
            result.asset.reset();
            add_error(result.errors, ErrorCode::missing_file, "source",
                      "vector source is missing");
        }
    }
    return result;
}

VectorAssetResult publish_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const VectorAsset& asset,
    const std::filesystem::path& validated_source) {
    VectorAssetResult result;
    auto validation = validate_vector_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    if (asset.source_kind != VectorSourceKind::linked_svg) {
        add_error(result.errors, ErrorCode::invalid_asset, "sourceKind",
                  "publish_vector_asset only publishes linked SVG imports");
        return result;
    }
    const auto document_relative = vector_document_path(
        manifest, asset.document.id);
    auto publication = detail::publish_asset_files(
        project_root, manifest.directories.assets / "vectors", asset.source,
        document_relative, validated_source, serialize_vector_asset(asset),
        "vector");
    if (!publication.ok()) {
        result.errors = std::move(publication.errors);
        return result;
    }
    return load_vector_asset(project_root, manifest, document_relative);
}

VectorAssetResult publish_native_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const VectorAsset& asset) {
    VectorAssetResult result;
    if (asset.source_kind != VectorSourceKind::native) {
        add_error(result.errors, ErrorCode::invalid_asset, "sourceKind",
                  "publish_native_vector_asset only publishes native vectors");
        return result;
    }
    auto validation = validate_vector_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    const auto document_relative = vector_document_path(
        manifest, asset.document.id);
    const auto serialized = serialize_vector_asset(asset);
    auto saved = save_document_atomic(
        project_root, document_relative, serialized,
        [&manifest](const std::string_view contents) {
            auto parsed = parse_vector_asset(manifest, contents);
            return ValidationReport{.errors = std::move(parsed.errors)};
        });
    if (!saved.ok()) {
        result.errors = std::move(saved.errors);
        return result;
    }
    return load_vector_asset(project_root, manifest, document_relative);
}

} // namespace fabric::project
