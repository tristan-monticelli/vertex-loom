#include "fabric/render/svg_vector.hpp"
#include "fabric/render/raster_image.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wfloat-conversion"
#endif
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace fabric::render {
namespace {

void add_error(project::VectorAssetResult& result, std::string field,
               std::string message) {
    result.errors.push_back(project::Error{project::ErrorCode::invalid_asset,
                                           std::move(field),
                                           std::move(message)});
}

core::Vec2 svg_point(const float x, const float y, const float height) {
    return {x, height - y};
}

core::Color svg_color(const unsigned int value, const float opacity) {
    return {static_cast<float>(value & 0xffU) / 255.0F,
            static_cast<float>((value >> 8U) & 0xffU) / 255.0F,
            static_cast<float>((value >> 16U) & 0xffU) / 255.0F,
            static_cast<float>((value >> 24U) & 0xffU) / 255.0F * opacity};
}

project::VectorStrokeJoin to_join(const char value) {
    switch (value) {
    case NSVG_JOIN_ROUND: return project::VectorStrokeJoin::round;
    case NSVG_JOIN_BEVEL: return project::VectorStrokeJoin::bevel;
    default: return project::VectorStrokeJoin::miter;
    }
}

project::VectorStrokeCap to_cap(const char value) {
    switch (value) {
    case NSVG_CAP_ROUND: return project::VectorStrokeCap::round;
    case NSVG_CAP_SQUARE: return project::VectorStrokeCap::square;
    default: return project::VectorStrokeCap::butt;
    }
}

} // namespace

project::VectorAssetResult convert_svg_to_native(
    const std::filesystem::path& path, const core::ResourceId& id,
    std::string name) {
    project::VectorAssetResult result;
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
    if (extension != ".svg") {
        add_error(result, "source", "source file must use the .svg extension");
        return result;
    }
    std::error_code status;
    const auto size = std::filesystem::file_size(path, status);
    if (status) {
        add_error(result, "source", "cannot inspect the SVG source");
        return result;
    }
    if (size > maximum_svg_source_bytes) {
        add_error(result, "source", "SVG source exceeds the 8 MiB safety limit");
        return result;
    }
    std::ifstream input(path, std::ios::binary);
    std::string source((std::istreambuf_iterator<char>(input)), {});
    if (!input && !input.eof()) {
        add_error(result, "source", "cannot read the SVG source");
        return result;
    }
    source.push_back('\0');
    NSVGimage* image = nsvgParse(source.data(), "px", 96.0F);
    if (image == nullptr || !std::isfinite(image->width) ||
        !std::isfinite(image->height) || image->width <= 0.0F ||
        image->height <= 0.0F) {
        if (image != nullptr) nsvgDelete(image);
        add_error(result, "source", "NanoSVG rejected the source or its dimensions");
        return result;
    }
    project::VectorAsset asset{
        .document = {.schema_version = project::current_vector_schema_version,
                     .type = "vector",
                     .id = id,
                     .name = std::move(name)},
        .source_kind = project::VectorSourceKind::native,
        .source = {},
        .native = project::NativeVectorDefinition{
            .size = {image->width, image->height},
            .origin = project::VectorOrigin::top_left,
        },
    };
    std::size_t shape_index = 0U;
    for (NSVGshape* shape = image->shapes; shape != nullptr;
         shape = shape->next, ++shape_index) {
        if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0) continue;
        if (shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT ||
            shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT ||
            shape->stroke.type == NSVG_PAINT_LINEAR_GRADIENT ||
            shape->stroke.type == NSVG_PAINT_RADIAL_GRADIENT) {
            add_error(result, "source",
                      "SVG gradients are not supported by native conversion");
            continue;
        }
        std::size_t path_index = 0U;
        for (NSVGpath* path_data = shape->paths; path_data != nullptr;
             path_data = path_data->next, ++path_index) {
            if (path_data->npts < 4 || path_data->pts == nullptr) {
                add_error(result, "source", "SVG path has no supported geometry");
                continue;
            }
            project::VectorShape vector_shape{
                .id = "shape-" + std::to_string(shape_index) + "-" +
                    std::to_string(path_index),
                .kind = project::VectorShapeKind::path,
            };
            const auto first = svg_point(path_data->pts[0], path_data->pts[1],
                                         image->height);
            vector_shape.path.push_back({.kind = project::VectorPathCommandKind::move,
                                         .point = first});
            for (int point = 1; point + 2 < path_data->npts; point += 3) {
                vector_shape.path.push_back({
                    .kind = project::VectorPathCommandKind::cubic,
                    .point = svg_point(path_data->pts[point * 2 + 4],
                                       path_data->pts[point * 2 + 5], image->height),
                    .control1 = svg_point(path_data->pts[point * 2],
                                          path_data->pts[point * 2 + 1], image->height),
                    .control2 = svg_point(path_data->pts[point * 2 + 2],
                                          path_data->pts[point * 2 + 3], image->height),
                });
            }
            if (path_data->closed) {
                vector_shape.path.push_back({
                    .kind = project::VectorPathCommandKind::close});
            }
            vector_shape.bounds = {
                .origin = {path_data->bounds[0],
                           image->height - path_data->bounds[3]},
                .size = {path_data->bounds[2] - path_data->bounds[0],
                         path_data->bounds[3] - path_data->bounds[1]},
            };
            project::VectorNode node{
                .id = vector_shape.id,
                .name = shape->id[0] == '\0' ? vector_shape.id : shape->id,
                .shape = std::move(vector_shape),
            };
            if (shape->fill.type == NSVG_PAINT_COLOR) {
                node.fill = {.kind = project::VectorFillKind::solid,
                             .color = svg_color(shape->fill.color, shape->opacity)};
            }
            if (shape->stroke.type == NSVG_PAINT_COLOR) {
                node.stroke = project::VectorStroke{
                    .color = svg_color(shape->stroke.color, shape->opacity),
                    .width = shape->strokeWidth,
                    .join = to_join(shape->strokeLineJoin),
                    .cap = to_cap(shape->strokeLineCap),
                };
            }
            asset.native->nodes.push_back(std::move(node));
        }
    }
    nsvgDelete(image);
    if (asset.native->nodes.empty()) {
        add_error(result, "source", "SVG contains no supported visible paths");
        return result;
    }
    result.asset = std::move(asset);
    return result;
}

} // namespace fabric::render
