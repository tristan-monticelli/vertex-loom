#include "resource_picker.hpp"

#include <SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fabric::map_studio {
namespace {

std::string_view resource_kind_label(
    const editor::StudioResourceKind kind) noexcept {
    using Kind = editor::StudioResourceKind;
    switch (kind) {
    case Kind::texture: return "texture";
    case Kind::vector: return "vector";
    case Kind::material: return "material";
    case Kind::entity: return "entity";
    case Kind::animation: return "animation";
    case Kind::input: return "input";
    case Kind::behavior: return "behavior";
    case Kind::transformation: return "transformation";
    case Kind::textured_path: return "textured path";
    case Kind::visual_composition: return "visual composition";
    case Kind::visual_component: return "visual component";
    case Kind::map: return "map";
    case Kind::scene: return "scene";
    case Kind::mechanic: return "mechanic";
    case Kind::replay: return "replay";
    case Kind::audio: return "audio";
    }
    return "unknown";
}

} // namespace

void draw_resource_picker(const char* label,
                          const std::filesystem::path& directory,
                          const std::string_view suffix,
                          std::string& selected_id,
                          editor::ProjectSession* catalog) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return;
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) selected_id.clear();
    ImGui::PushID(label);
    static std::unordered_map<std::string, std::string> filters;
    auto& filter = filters[label];
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputText("Search##resource-picker-search", &filter);
    std::vector<std::string> resource_ids;
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) continue;
        auto filename = iterator->path().filename().string();
        if (!filename.ends_with(suffix)) continue;
        filename.resize(filename.size() - suffix.size());
        if (!filter.empty()) {
            auto haystack = filename;
            auto needle = filter;
            std::ranges::transform(
                haystack, haystack.begin(), [](const unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            std::ranges::transform(
                needle, needle.begin(), [](const unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            if (haystack.find(needle) == std::string::npos) continue;
        }
        resource_ids.push_back(std::move(filename));
    }
    std::ranges::sort(resource_ids);
    if (ImGui::TreeNodeEx("Resources##resource-picker-tree",
                          ImGuiTreeNodeFlags_DefaultOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (const auto& resource_id : resource_ids) {
            const auto item_label = resource_id +
                "##resource-picker-item-" + resource_id;
            if (ImGui::Selectable(item_label.c_str(),
                                  selected_id == resource_id)) {
                selected_id = resource_id;
            }
            if (selected_id == resource_id) ImGui::SetItemDefaultFocus();
        }
        if (resource_ids.empty()) ImGui::TextDisabled("No matching resource.");
        ImGui::TreePop();
    }
    if (!selected_id.empty()) {
        const auto selected_path =
            directory / (selected_id + std::string{suffix});
        if (std::filesystem::is_regular_file(selected_path, error)) {
            const editor::StudioResource* catalog_resource = nullptr;
            if (catalog != nullptr) {
                const auto resource = std::ranges::find_if(
                    catalog->resources(), [&](const auto& item) {
                        return item.id.value == selected_id &&
                            item.document_path ==
                                selected_path.lexically_relative(
                                    catalog->project_root());
                    });
                if (resource != catalog->resources().end())
                    catalog_resource = &*resource;
            }
            const auto type = catalog_resource == nullptr
                ? std::string{suffix}
                : std::string{resource_kind_label(catalog_resource->kind)};
            ImGui::TextDisabled("Type: %s", type.c_str());
            ImGui::TextDisabled("Path: %s",
                                selected_path.generic_string().c_str());
            const auto size = std::filesystem::file_size(selected_path, error);
            if (!error) {
                ImGui::TextDisabled("Size: %llu bytes",
                    static_cast<unsigned long long>(size));
            } else {
                ImGui::TextDisabled("Size: n/a");
            }
            ImGui::TextDisabled("Thumbnail: n/a (document resource)");
            const auto dimensions = catalog_resource != nullptr &&
                    catalog_resource->width != 0U &&
                    catalog_resource->height != 0U
                ? std::to_string(catalog_resource->width) + "x" +
                    std::to_string(catalog_resource->height)
                : std::string{"n/a"};
            ImGui::TextDisabled("Dimensions: %s", dimensions.c_str());
            ImGui::TextDisabled(
                "Format: %s",
                catalog_resource != nullptr &&
                        !catalog_resource->format.empty()
                    ? catalog_resource->format.c_str()
                    : std::string{suffix}.c_str());
            if (catalog_resource != nullptr && catalog != nullptr) {
                const auto references = catalog->incoming_references(
                    catalog_resource->kind, catalog_resource->id);
                const auto count = references
                    ? std::to_string(references->size())
                    : std::string{"unavailable"};
                ImGui::TextDisabled(
                    "Dependencies / incoming references: %s", count.c_str());
            } else {
                ImGui::TextDisabled(
                    "Dependencies / incoming references: unavailable");
            }
            if (ImGui::SmallButton("Open##resource-picker-open")) {
                const auto absolute =
                    std::filesystem::absolute(selected_path, error);
                if (!error) {
                    const auto url = "file://" + absolute.generic_string();
                    SDL_OpenURL(url.c_str());
                }
            }
        } else {
            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                               "Missing resource: %s", selected_id.c_str());
        }
    }
    ImGui::PopID();
}

} // namespace fabric::map_studio
