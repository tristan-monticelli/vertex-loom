#include "fabric/project/manifest.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/render/vector_geometry.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string_view>

namespace {

using Json = nlohmann::json;

Json point_json(const fabric::core::Vec2 point) {
    return {{"x", point.x}, {"y", point.y}};
}

void print_errors(const std::vector<fabric::project::Error>& errors) {
    for (const auto& error : errors) {
        std::cerr << fabric::project::to_string(error.code) << " ["
                  << error.field << "]: " << error.message << '\n';
    }
}

} // namespace

int main(const int argument_count, char** arguments) {
    if (argument_count != 3) {
        std::cerr << "usage: fabric_asset_preview <project-directory> <vector-id>\n";
        return 64;
    }
    const fabric::core::ResourceId id{.value = arguments[2]};
    if (!fabric::core::ResourceId::is_valid(id.value)) {
        std::cerr << "invalid vector resource id\n";
        return 64;
    }
    const auto manifest = fabric::project::load_manifest(arguments[1]);
    if (!manifest.ok()) {
        print_errors(manifest.errors);
        return 1;
    }
    const auto document = fabric::project::load_vector_asset(
        arguments[1], *manifest.manifest,
        fabric::project::vector_document_path(*manifest.manifest, id));
    if (!document.ok()) {
        print_errors(document.errors);
        return 1;
    }
    const auto packets = fabric::render::build_native_draw_packets(*document.asset);
    if (!packets.ok()) {
        for (const auto& error : packets.errors) std::cerr << error << '\n';
        return 1;
    }
    Json output = {
        {"id", document.asset->document.id.value},
        {"name", document.asset->document.name},
        {"sourceKind", std::string(fabric::project::to_string(
                          document.asset->source_kind))},
        {"packets", Json::array()},
    };
    for (const auto& packet : packets.packets) {
        Json packet_json = {
            {"nodeId", packet.node_id},
            {"fillVertices", Json::array()},
            {"fillIndices", packet.fill_indices},
            {"outline", Json::array()},
            {"closedOutline", packet.closed_outline},
        };
        for (const auto point : packet.fill_vertices) {
            packet_json["fillVertices"].push_back(point_json(point));
        }
        for (const auto point : packet.outline) {
            packet_json["outline"].push_back(point_json(point));
        }
        if (packet.fill_color.has_value()) {
            packet_json["fillColor"] = {
                {"red", packet.fill_color->red},
                {"green", packet.fill_color->green},
                {"blue", packet.fill_color->blue},
                {"alpha", packet.fill_color->alpha},
            };
        }
        if (packet.image_fill.has_value()) {
            packet_json["imageTexture"] = packet.image_fill->texture.id.value;
            packet_json["fillUv"] = Json::array();
            for (const auto uv : packet.fill_uv) {
                packet_json["fillUv"].push_back(point_json(uv));
            }
        }
        output["packets"].push_back(std::move(packet_json));
    }
    std::cout << output.dump(2) << '\n';
    return 0;
}
