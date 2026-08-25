#include "fabric/editor/map_session.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_stdlib.h>

#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void draw_errors(const fabric::editor::MapSession& session) {
    for (const auto& error : session.errors()) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("%s: %s", error.field.c_str(), error.message.c_str());
        ImGui::PopStyleColor();
    }
}

std::optional<fabric::project::MapPropertyValue> parse_override_value(
    const int kind, const std::string_view text) {
    try {
        switch (kind) {
        case 0:
            if (text == "true") return true;
            if (text == "false") return false;
            return std::nullopt;
        case 1: {
            std::size_t consumed = 0;
            const auto value = std::stoll(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return static_cast<std::int64_t>(value);
        }
        case 2: {
            std::size_t consumed = 0;
            const auto value = std::stof(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return value;
        }
        case 3:
            return std::string{text};
        case 4: {
            const auto separator = text.find(',');
            if (separator == std::string_view::npos) return std::nullopt;
            std::size_t consumed_x = 0;
            std::size_t consumed_y = 0;
            const auto x = std::stof(std::string{text.substr(0, separator)}, &consumed_x);
            const auto y = std::stof(std::string{text.substr(separator + 1)}, &consumed_y);
            if (consumed_x != separator || consumed_y != text.size() - separator - 1U)
                return std::nullopt;
            return fabric::core::Vec2{x, y};
        }
        case 5:
            if (text.empty()) return std::nullopt;
            return fabric::project::ResourceReference{{.value = std::string{text}}, "resource"};
        default:
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int run(const std::filesystem::path& project_root,
        const fabric::core::ResourceId& map_id) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    auto* window = SDL_CreateWindow("Vertex Loom Map Studio",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1200, 760,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    auto context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
#if defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    fabric::editor::MapSession session;
    std::string status;
    if (!project_root.empty()) {
        if (!session.open(project_root, map_id)) status = "Map could not be opened";
        else status = "Map opened";
    }
    std::string event_id;
    std::vector<std::string> selected_instances;
    std::string selected_prefab;
    std::string override_id;
    std::string override_value;
    int override_kind = 2;
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Map Studio", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse);
        if (!session.has_map()) {
            ImGui::TextWrapped("Open a map with: map_studio <project-directory> <map-id>");
            draw_errors(session);
        } else {
            const auto& map = *session.map();
            ImGui::Text("Map: %s (%s)", map.document.name.c_str(),
                        map.document.id.value.c_str());
            ImGui::SameLine();
            ImGui::TextColored(session.dirty() ? ImVec4{1.0F, 0.75F, 0.25F, 1.0F}
                                               : ImVec4{0.45F, 0.9F, 0.55F, 1.0F},
                               session.dirty() ? "dirty" : "saved");
            if (ImGui::Button("Save")) status = session.save() ? "Map saved" : "Save failed";
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.can_undo());
            if (ImGui::Button("Undo")) static_cast<void>(session.undo());
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.can_redo());
            if (ImGui::Button("Redo")) static_cast<void>(session.redo());
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::Columns(2, "map-studio-columns", true);
            ImGui::Text("Layers (%zu)", map.layers.size());
            bool layer_changed = false;
            for (std::size_t layer_index = 0; layer_index < map.layers.size(); ++layer_index) {
                const auto layer = map.layers[layer_index];
                ImGui::PushID(layer.id.c_str());
                bool visible = layer.visible;
                if (ImGui::Checkbox("##visible", &visible) &&
                    session.set_layer_visibility({.value = layer.id}, visible)) {
                    status = "Layer visibility changed";
                    layer_changed = true;
                }
                ImGui::SameLine();
                bool locked = layer.locked;
                if (ImGui::Checkbox("##locked", &locked) &&
                    session.set_layer_locked({.value = layer.id}, locked)) {
                    status = "Layer lock changed";
                    layer_changed = true;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(layer.name.c_str());
                ImGui::SameLine();
                float depth = layer.depth;
                ImGui::SetNextItemWidth(90.0F);
                if (ImGui::DragFloat("##depth", &depth, 0.1F) &&
                    ImGui::IsItemDeactivatedAfterEdit() &&
                    session.set_layer_depth({.value = layer.id}, depth)) {
                    status = "Layer depth changed";
                    layer_changed = true;
                }
                ImGui::PopID();
                if (layer_changed) break;
            }
            if (layer_changed) ImGui::TextDisabled("Layer edit recorded in undo history");
            ImGui::SeparatorText("Content");
            ImGui::Text("Instances: %zu", map.instances.size());
            for (const auto& instance : map.instances) {
                bool selected = std::find(selected_instances.begin(), selected_instances.end(),
                                          instance.id) != selected_instances.end();
                if (ImGui::Checkbox((instance.id + "##selected").c_str(), &selected)) {
                    if (selected) selected_instances.push_back(instance.id);
                    else selected_instances.erase(std::remove(selected_instances.begin(),
                                                               selected_instances.end(), instance.id),
                                                  selected_instances.end());
                }
            }
            ImGui::BeginDisabled(selected_instances.empty());
            if (ImGui::Button("Nudge selected +1")) {
                std::vector<fabric::core::ResourceId> ids;
                for (const auto& id : selected_instances) ids.push_back({.value = id});
                status = session.translate_instances(ids, {1.0F, 0.0F})
                    ? "Selected instances moved" : "Selection contains a locked instance";
            }
            if (selected_instances.size() == 1U) {
                ImGui::SameLine();
                const fabric::core::ResourceId selected_id{selected_instances.front()};
                if (ImGui::Button("Duplicate selected")) {
                    status = session.duplicate_instance(selected_id)
                        ? "Instance duplicated" : "Instance duplication rejected";
                }
                ImGui::SameLine();
                if (ImGui::Button("Move selected to front")) {
                    status = session.reorder_instance(selected_id, 0U)
                        ? "Instance reordered" : "Instance reorder rejected";
                }
            }
            ImGui::EndDisabled();
            ImGui::Text("Collisions: %zu", map.collisions.size());
            ImGui::Text("Triggers: %zu", map.triggers.size());
            ImGui::Text("Events: %zu", map.events.size());
            ImGui::NextColumn();
            ImGui::SeparatorText("Events");
            ImGui::SetNextItemWidth(250.0F);
            ImGui::InputText("New event id", &event_id);
            ImGui::SameLine();
            ImGui::BeginDisabled(event_id.empty());
            if (ImGui::Button("Declare")) {
                if (session.declare_event({{.value = event_id}, {}})) {
                    status = "Event declared";
                    event_id.clear();
                } else status = "Event declaration rejected";
            }
            ImGui::EndDisabled();
            for (const auto& event_definition : map.events)
                ImGui::BulletText("%s", event_definition.id.value.c_str());
            ImGui::SeparatorText("Triggers");
            for (const auto& trigger : map.triggers)
                ImGui::BulletText("%s -> %s", trigger.id.c_str(), trigger.event_id.value.c_str());
            ImGui::SeparatorText("Prefab overrides");
            for (const auto& prefab : map.prefabs) {
                const auto selected = selected_prefab == prefab.id;
                if (ImGui::Selectable(prefab.id.c_str(), selected)) selected_prefab = prefab.id;
            }
            if (!selected_prefab.empty()) {
                ImGui::Text("Selected prefab: %s", selected_prefab.c_str());
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Property id", &override_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Type", &override_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Value", &override_value);
                ImGui::BeginDisabled(override_id.empty() || override_value.empty());
                if (ImGui::Button("Apply override")) {
                    const auto value = parse_override_value(override_kind, override_value);
                    const auto applied = value && session.set_prefab_override(
                        {.value = selected_prefab}, {override_id, *value});
                    status = applied ? "Prefab override applied" : "Prefab override rejected";
                    if (applied) {
                        override_id.clear();
                        override_value.clear();
                    }
                }
                ImGui::EndDisabled();
            }
            ImGui::Columns(1);
            if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
            draw_errors(session);
        }
        ImGui::End();
        ImGui::Render();
        glViewport(0, 0, static_cast<GLsizei>(ImGui::GetIO().DisplaySize.x),
                   static_cast<GLsizei>(ImGui::GetIO().DisplaySize.y));
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) {
        std::cerr << "usage: map_studio [project-directory map-id]\n";
        return 64;
    }
    const std::filesystem::path project = argc == 3 ? argv[1] : std::filesystem::path{};
    const fabric::core::ResourceId map_id{argc == 3 ? argv[2] : ""};
    return run(project, map_id);
}
