#include "fabric/editor/map_session.hpp"
#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/editor_action_registry.hpp"
#include "fabric/editor/editor_layout_preferences.hpp"
#include "fabric/editor/mechanic_presets.hpp"
#include "fabric/editor/mechanic_session.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/editor/scene_session.hpp"
#include "fabric/editor/session_transition.hpp"
#include "fabric/editor/studio_workspace.hpp"
#include "fabric/editor/transformation_session.hpp"
#include "fabric/project/document_storage.hpp"
#include "fabric/project/map_package.hpp"
#include "fabric/project/entity_transformation.hpp"
#include "fabric/render/map_preview.hpp"
#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/runtime/preview_runtime.hpp"
#include "fabric/runtime/scene_session.hpp"
#include "editor_widgets.hpp"
#include "map_canvas.hpp"
#include "mechanic_workspace.hpp"
#include "publish_workspace.hpp"
#include "resource_picker.hpp"
#include "scene_workspace.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_stdlib.h>
#include <nfd.h>
#include <nfd_sdl2.h>

#include <filesystem>
#include <array>
#include <cctype>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <limits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {

using fabric::editor_ui::draw_disabled_reason;
using fabric::editor_ui::draw_id_picker;
using fabric::editor_ui::draw_field_errors;
using fabric::editor_ui::draw_command_palette;
using fabric::editor_ui::draw_document_navigation;
using fabric::editor_ui::draw_resource_name_field;
using fabric::editor_ui::draw_searchable_id_picker;
using fabric::editor_ui::draw_technical_tooltip;
using fabric::editor_ui::draw_validation_errors;
using fabric::editor_ui::focus_first_field_error;
using fabric::editor_ui::SearchableIdOption;
using fabric::editor_ui::SearchableIdPickerOptions;
using fabric::map_studio::draw_resource_picker;
using fabric::map_studio::draw_map_canvas;
using fabric::map_studio::draw_mechanic_workspace;
using fabric::map_studio::draw_publish_workspace;
using fabric::map_studio::draw_scene_workspace;
using fabric::map_studio::draw_transform_editor;
using fabric::map_studio::collision_shape_text;
using fabric::map_studio::CanvasGizmoState;
using fabric::map_studio::CollisionPointGizmoState;
using fabric::map_studio::MapPreviewRenderer;
using fabric::map_studio::MapPlacementProbe;
using fabric::map_studio::MapMechanicOverlayState;
using fabric::map_studio::MapTexture;
using fabric::map_studio::MechanicWorkspaceProbe;
using fabric::map_studio::MechanicWorkspaceState;
using fabric::map_studio::PublishWorkspaceProbe;
using fabric::map_studio::PublishWorkspaceState;
using fabric::map_studio::SelectionBoxState;
using fabric::map_studio::SceneWorkspaceState;
using fabric::map_studio::TransformEditorState;

bool ui_map_workspace_seen = false;
float ui_map_layers_x = 0.0F;
float ui_map_canvas_x = 0.0F;
float ui_map_inspector_x = 0.0F;
float ui_map_canvas_width = 0.0F;

std::vector<fabric::project::EntityTransformation> load_transformations(
    const std::filesystem::path& root,
    const fabric::project::ProjectManifest& manifest) {
    std::vector<fabric::project::EntityTransformation> result;
    const auto directory = root / manifest.directories.assets / "transformations";
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return result;
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        const auto filename = iterator->path().filename().string();
        if (!iterator->is_regular_file(error) ||
            !filename.ends_with(".transformation.json")) continue;
        auto loaded = fabric::project::load_entity_transformation(
            root, manifest, iterator->path().lexically_relative(root));
        if (loaded.ok()) result.push_back(std::move(*loaded.asset));
    }
    std::ranges::sort(result, {}, [](const auto& value) {
        return value.document.name;
    });
    return result;
}

enum class CloseE2eMode { clean, window, system_shortcut, save, save_failure };

std::optional<CloseE2eMode> close_e2e_mode(const std::string_view value) {
    if (value == "clean") return CloseE2eMode::clean;
    if (value == "window") return CloseE2eMode::window;
    if (value == "shortcut") return CloseE2eMode::system_shortcut;
    if (value == "save") return CloseE2eMode::save;
    if (value == "save-failure") return CloseE2eMode::save_failure;
    return std::nullopt;
}

std::optional<std::string> read_binary_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
}

void write_e2e_failure_artifacts(
    const std::filesystem::path& project_path, SDL_Window* window,
    const std::string& status,
    const fabric::editor::MapSession& session,
    const std::vector<fabric::project::Error>& package_errors) {
    if (project_path.empty()) return;

    std::ofstream report(project_path / "map_studio-e2e-failure.txt");
    if (report) {
        report << "status: " << status << '\n';
        for (const auto& error : session.errors()) {
            report << error.field << " | " << error.message << '\n';
        }
        for (const auto& error : package_errors) {
            report << "package:" << error.field << " | " << error.message << '\n';
        }
    }

    if (window == nullptr) return;
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) return;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream image(project_path / "map_studio-e2e-failure.ppm",
                        std::ios::binary);
    if (!image) return;
    image << "P6\n" << width << ' ' << height << "\n255\n";
    const auto row_size = static_cast<std::size_t>(width) * 3U;
    for (int row = height - 1; row >= 0; --row) {
        image.write(reinterpret_cast<const char*>(pixels.data() +
                                                   static_cast<std::size_t>(row) *
                                                       row_size),
                    static_cast<std::streamsize>(row_size));
    }
}

void write_frame_capture(const std::filesystem::path& project_path,
                         SDL_Window* window, const std::string_view filename) {
    if (project_path.empty() || window == nullptr) return;
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) return;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream image(project_path / filename, std::ios::binary);
    if (!image) return;
    image << "P6\n" << width << ' ' << height << "\n255\n";
    const auto row_size = static_cast<std::size_t>(width) * 3U;
    for (int row = height - 1; row >= 0; --row)
        image.write(reinterpret_cast<const char*>(
                        pixels.data() + static_cast<std::size_t>(row) * row_size),
                    static_cast<std::streamsize>(row_size));
}

float relative_luminance(const ImVec4 color) {
    const auto linear = [](const float channel) {
        return channel <= 0.03928F ? channel / 12.92F
            : std::pow((channel + 0.055F) / 1.055F, 2.4F);
    };
    return 0.2126F * linear(color.x) + 0.7152F * linear(color.y) +
        0.0722F * linear(color.z);
}

void write_ui_accessibility_probe(const std::filesystem::path& project_path,
                                  SDL_Window* window,
                                  const bool keyboard_navigation_enabled,
                                  const bool command_palette_rendered) {
    if (project_path.empty() || window == nullptr) return;
    const auto& colors = ImGui::GetStyle().Colors;
    const float background = relative_luminance(colors[ImGuiCol_WindowBg]);
    const float text = relative_luminance(colors[ImGuiCol_Text]);
    const float contrast = (std::max(background, text) + 0.05F) /
        (std::min(background, text) + 0.05F);
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) return;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const auto [minimum, maximum] = std::minmax_element(pixels.begin(), pixels.end());
    const bool visual_valid = width >= 960 && height >= 640 &&
        minimum != pixels.end() && maximum != pixels.end() &&
        *minimum != *maximum;
    std::ofstream output(project_path / "map-studio-ui-accessibility.json");
    if (output) {
        output << "{\n  \"schema\": \"map-studio-ui-accessibility-test-v1\",\n"
               << "  \"keyboard_navigation_enabled\": "
               << (keyboard_navigation_enabled ? "true" : "false") << ",\n"
               << "  \"command_palette_rendered\": "
               << (command_palette_rendered ? "true" : "false") << ",\n"
               << "  \"text_window_contrast\": " << contrast << ",\n"
               << "  \"text_window_contrast_ok\": "
               << (contrast >= 4.5F ? "true" : "false") << ",\n"
               << "  \"capture_width\": " << width << ",\n"
               << "  \"capture_height\": " << height << ",\n"
               << "  \"workspace_rendered\": "
               << (ui_map_workspace_seen ? "true" : "false") << ",\n"
               << "  \"layers_canvas_inspector_order\": "
               << (ui_map_layers_x < ui_map_canvas_x &&
                           ui_map_canvas_x < ui_map_inspector_x
                       ? "true" : "false") << ",\n"
               << "  \"canvas_width\": " << ui_map_canvas_width << ",\n"
               << "  \"canvas_minimum_width_ok\": "
               << (ui_map_canvas_width >= 320.0F ? "true" : "false") << ",\n"
               << "  \"visual_valid\": "
               << (visual_valid ? "true" : "false") << "\n}\n";
    }
    std::ofstream image(project_path / "map_studio-ui-accessibility.ppm",
                        std::ios::binary);
    if (!image) return;
    image << "P6\n" << width << ' ' << height << "\n255\n";
    const auto row_size = static_cast<std::size_t>(width) * 3U;
    for (int row = height - 1; row >= 0; --row)
        image.write(reinterpret_cast<const char*>(pixels.data() +
                                                   static_cast<std::size_t>(row) * row_size),
                    static_cast<std::streamsize>(row_size));
}

std::optional<std::filesystem::path> choose_folder(SDL_Window* window,
                                                    std::string& status) {
    nfdu8char_t* selected_path = nullptr;
    nfdpickfolderu8args_t arguments{};
    NFD_GetNativeWindowFromSDLWindow(window, &arguments.parentWindow);
    const auto result = NFD_PickFolderU8_With(&selected_path, &arguments);
    if (result == NFD_CANCEL) return std::nullopt;
    if (result == NFD_ERROR) {
        status = "Native folder dialog failed: " +
            std::string(NFD_GetError() == nullptr ? "unknown error"
                                                   : NFD_GetError());
        return std::nullopt;
    }
    const std::filesystem::path path{selected_path};
    NFD_FreePathU8(selected_path);
    return path;
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

std::string_view resource_contract_for_kind(const int kind) {
    using ResourceKind = fabric::editor::StudioResourceKind;
    switch (static_cast<ResourceKind>(kind)) {
    case ResourceKind::texture: return "texture";
    case ResourceKind::vector: return "vector";
    case ResourceKind::material: return "material";
    case ResourceKind::entity: return "entity";
    case ResourceKind::animation: return "animation";
    case ResourceKind::input: return "input";
    case ResourceKind::behavior: return "behavior";
    case ResourceKind::transformation: return "transformation";
    case ResourceKind::textured_path: return "texturedPath";
    case ResourceKind::visual_composition: return "visualComposition";
    case ResourceKind::visual_component: return "visualComponent";
    case ResourceKind::map: return "map";
    case ResourceKind::scene: return "scene";
    case ResourceKind::mechanic: return "mechanic";
    case ResourceKind::replay: return "replay";
    case ResourceKind::audio: return "audio";
    }
    return "resource";
}

std::optional<fabric::project::MechanicValue> parse_mechanic_override_value(
    const fabric::project::MechanicParameterDefinition& parameter,
    const std::string_view text) {
    try {
        using Type = fabric::project::MechanicValueType;
        switch (parameter.type) {
        case Type::boolean:
            if (text == "true") return true;
            if (text == "false") return false;
            return std::nullopt;
        case Type::integer: {
            std::size_t consumed = 0;
            const auto value = std::stoll(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return static_cast<std::int64_t>(value);
        }
        case Type::scalar: {
            std::size_t consumed = 0;
            const auto value = std::stof(std::string{text}, &consumed);
            if (consumed != text.size() || !std::isfinite(value))
                return std::nullopt;
            return value;
        }
        case Type::text: return std::string{text};
        case Type::vec2: {
            const auto separator = text.find(',');
            if (separator == std::string_view::npos) return std::nullopt;
            std::size_t consumed_x = 0;
            std::size_t consumed_y = 0;
            const auto x = std::stof(std::string{text.substr(0, separator)},
                                     &consumed_x);
            const auto y = std::stof(std::string{text.substr(separator + 1)},
                                     &consumed_y);
            if (consumed_x != separator ||
                consumed_y != text.size() - separator - 1U ||
                !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
            return fabric::core::Vec2{x, y};
        }
        case Type::resource: {
            if (text.empty()) return std::nullopt;
            const auto* fallback = std::get_if<fabric::project::ResourceReference>(
                &parameter.default_value);
            return fabric::project::ResourceReference{
                {.value = std::string{text}},
                fallback == nullptr ? "resource" : fallback->expected_type};
        }
        case Type::body_handle:
        case Type::pivot_handle:
        case Type::joint_handle: return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<fabric::core::ResourceId> instance_mechanic_id(
    const fabric::project::MapDocument& map,
    const std::string_view instance_id) {
    const auto instance = std::ranges::find(
        map.instances, instance_id, &fabric::project::MapInstance::id);
    if (instance == map.instances.end() || !instance->prefab)
        return std::nullopt;
    const auto prefab = std::ranges::find(
        map.prefabs, instance->prefab->id.value,
        &fabric::project::PrefabDefinition::id);
    if (prefab == map.prefabs.end() || !prefab->mechanic)
        return std::nullopt;
    return prefab->mechanic->id;
}

void draw_value_parse_error(
    const std::optional<fabric::project::MapPropertyValue>& value,
    const std::string_view field,
    const std::string_view correction) {
    if (value.has_value()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, {0.98F, 0.48F, 0.42F, 1.0F});
    ImGui::TextWrapped("%s: value has the wrong format", std::string(field).c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Correction: %s", std::string(correction).c_str());
}

std::string property_value_text(const fabric::project::MapPropertyValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, bool>) return item ? "true" : "false";
        else if constexpr (std::is_same_v<Value, std::int64_t>) return std::to_string(item);
        else if constexpr (std::is_same_v<Value, float>) return std::to_string(item);
        else if constexpr (std::is_same_v<Value, std::string>) return item;
        else if constexpr (std::is_same_v<Value, fabric::core::Vec2>)
            return std::to_string(item.x) + "," + std::to_string(item.y);
        else return item.id.value;
    }, value);
}

bool draw_typed_resource_id_picker(
    const char* label,
    const std::span<const fabric::editor::StudioResource> resources,
    int& resource_kind,
    std::string& selected_id) {
    using ResourceKind = fabric::editor::StudioResourceKind;
    constexpr std::array<ResourceKind, 16> kinds{
        ResourceKind::texture, ResourceKind::vector, ResourceKind::material,
        ResourceKind::entity, ResourceKind::animation, ResourceKind::input,
        ResourceKind::behavior, ResourceKind::transformation,
        ResourceKind::textured_path, ResourceKind::visual_composition,
        ResourceKind::visual_component, ResourceKind::map, ResourceKind::scene,
        ResourceKind::mechanic, ResourceKind::replay, ResourceKind::audio};
    bool changed = false;
    const auto kind_label = resource_contract_for_kind(resource_kind);
    if (ImGui::BeginCombo((std::string{label} + " type").c_str(),
                          kind_label.data())) {
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            const bool selected = static_cast<int>(index) == resource_kind;
            if (ImGui::Selectable(resource_contract_for_kind(
                                      static_cast<int>(index)).data(), selected)) {
                resource_kind = static_cast<int>(index);
                selected_id.clear();
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    std::vector<std::string> ids;
    for (const auto& resource : resources)
        if (resource.kind == kinds[static_cast<std::size_t>(resource_kind)])
            ids.push_back(resource.id.value);
    changed |= draw_id_picker(label, ids, selected_id,
                              "Choose a resource identifier...");
    return changed;
}

int run(const std::filesystem::path& project_root,
        const fabric::core::ResourceId& map_id,
        const std::optional<CloseE2eMode> e2e_mode = std::nullopt,
        const bool scene_e2e = false,
        const bool transformation_e2e = false,
        const bool ui_accessibility_test = false,
        const bool mechanic_e2e = false,
        const bool placement_e2e = false) {
    const auto trace_session_id =
        fabric::core::make_trace_session_id("map-studio");
    const bool graphical_test = e2e_mode.has_value() || scene_e2e ||
        transformation_e2e || ui_accessibility_test || mechanic_e2e ||
        placement_e2e;
    const int graphical_failure = graphical_test ? 77 : 1;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << SDL_GetError() << '\n';
        return graphical_failure;
    }
    if (NFD_Init() != NFD_OKAY) {
        std::cerr << "native file dialog initialization failed: "
                  << (NFD_GetError() == nullptr ? "unknown error"
                                                 : NFD_GetError()) << '\n';
        SDL_Quit();
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
    const auto window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
        (e2e_mode || scene_e2e || transformation_e2e || ui_accessibility_test ||
         mechanic_e2e || placement_e2e
             ? SDL_WINDOW_HIDDEN : 0U);
    auto* window = SDL_CreateWindow("Vertex Loom Map Studio",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1200, 760,
        window_flags);
    if (window == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        NFD_Quit();
        SDL_Quit();
        return graphical_failure;
    }
    SDL_SetWindowMinimumSize(window, 960, 640);
    auto context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        NFD_Quit();
        SDL_Quit();
        return graphical_failure;
    }
    SDL_GL_MakeCurrent(window, context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imgui_io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
#if defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    fabric::editor::MapSession session;
    fabric::editor::EditorContext editor_context;
    fabric::editor::EditorLayoutPreferences layout{
        .primary_panel_width = 260.0F,
        .secondary_panel_width = 340.0F,
    };
    std::filesystem::path layout_preferences_path;
    if (!graphical_test) {
        if (char* preference_root =
                SDL_GetPrefPath("Vertex Loom", "Map Studio")) {
            layout_preferences_path =
                std::filesystem::path{preference_root} / "layout.json";
            SDL_free(preference_root);
            if (const auto stored = fabric::editor::load_layout_preferences(
                    layout_preferences_path)) {
                layout = *stored;
            }
        }
    }
    fabric::editor::ProjectSession resource_catalog;
    fabric::editor::MechanicSession mechanic_session;
    fabric::editor::SceneSession scene_session;
    fabric::editor::SessionTransitionGuard transition_guard;
    MechanicWorkspaceState mechanic_editor;
    MechanicWorkspaceProbe mechanic_probe;
    PublishWorkspaceState publish_editor;
    PublishWorkspaceProbe publish_probe;
    SceneWorkspaceState scene_editor;
    enum class ActiveWorkspace { map, scene, mechanic, publish };
    ActiveWorkspace active_workspace = scene_e2e
        ? ActiveWorkspace::scene : ActiveWorkspace::map;
    fabric::render::OpenGLVectorRenderer map_renderer;
    std::unordered_map<std::string, MapTexture> map_textures;
    if (!map_renderer.initialize()) {
        std::cerr << "map OpenGL vector renderer initialization failed: "
                  << map_renderer.initialization_error() << '\n';
    }
    std::string status;
    std::string new_map_id;
    std::string new_map_name;
    std::string open_map_id;
    std::vector<fabric::project::Error> package_errors;
    const auto save_dirty_documents = [&] {
        return (!mechanic_session.dirty() || mechanic_session.save()) &&
            (!session.dirty() || session.save()) &&
            (!scene_session.dirty() || scene_session.save());
    };
    fabric::editor::EditorActionRegistry actions;
    static_cast<void>(actions.register_action({
        .id = std::string{fabric::editor::editor_action_ids::save},
        .label = "Save",
        .shortcut = "Ctrl+S",
        .availability = [&] {
            return fabric::editor::EditorActionAvailability{
                .enabled = session.has_map(),
                .disabled_reason = "Open or create a map before saving.",
            };
        },
        .execute = [&] {
            const bool saved = save_dirty_documents();
            status = saved ? "Documents saved" : "Save failed";
            return saved;
        },
    }));
    static_cast<void>(actions.register_action({
        .id = std::string{fabric::editor::editor_action_ids::undo},
        .label = "Undo",
        .shortcut = "Ctrl+Z",
        .availability = [&] {
            return fabric::editor::EditorActionAvailability{
                .enabled = session.can_undo(),
                .disabled_reason =
                    "No map changes are available to undo.",
            };
        },
        .execute = [&] {
            const bool undone = session.undo();
            if (undone) status = "Map change undone";
            return undone;
        },
    }));
    static_cast<void>(actions.register_action({
        .id = std::string{fabric::editor::editor_action_ids::redo},
        .label = "Redo",
        .shortcut = "Ctrl+Y",
        .availability = [&] {
            return fabric::editor::EditorActionAvailability{
                .enabled = session.can_redo(),
                .disabled_reason =
                    "No undone map changes are available to redo.",
            };
        },
        .execute = [&] {
            const bool redone = session.redo();
            if (redone) status = "Map change redone";
            return redone;
        },
    }));
    const auto prepare_package = [&] {
        if (!save_dirty_documents()) {
            status = "A document save failed; package action cancelled";
            return false;
        }
        return true;
    };
    if (!project_root.empty()) {
        if (!session.open(project_root, map_id)) status = "Map could not be opened";
        else status = "Map opened";
        static_cast<void>(resource_catalog.open(project_root));
    }
    bool e2e_failed = false;
    bool e2e_event_injected = false;
    bool e2e_modal_handled = false;
    std::filesystem::path e2e_primary_path;
    std::filesystem::path e2e_autosave_path;
    std::optional<std::string> e2e_primary_contents;
    std::optional<std::string> e2e_autosave_contents;
    const auto fail_e2e = [&](const std::string_view message) {
        std::cerr << "Map Studio close E2E: " << message << '\n';
        e2e_failed = true;
    };
    bool mechanic_e2e_complete = false;
    bool mechanic_authoring_verified = false;
    bool mechanic_map_parameter_verified = false;
    bool mechanic_map_reopen_verified = false;
    bool mechanic_connection_removed = false;
    std::size_t mechanic_e2e_frame = 0U;
    std::size_t mechanic_map_e2e_frame = 0U;
    std::size_t publish_e2e_frame = 0U;
    std::optional<ImVec2> mechanic_resize_drag_origin;
    std::optional<ImVec2> mechanic_rotation_drag_origin;
    std::optional<ImVec2> mechanic_joint_drag_origin;
    std::optional<ImVec2> mechanic_map_resize_drag_origin;
    bool mechanic_e2e_capture_written = false;
    bool mechanic_map_overlay_capture_written = false;
    bool publish_e2e_capture_written = false;
    if (mechanic_e2e) {
        mechanic_probe.enabled = true;
        const bool opened = session.map() && mechanic_session.open(
            project_root, *session.map(), {.value = "rotating-platform"});
        if (opened && !mechanic_session.graph()->connections.empty()) {
            mechanic_probe.expected_connection =
                mechanic_session.graph()->connections.front();
            mechanic_e2e_complete = true;
            mechanic_editor.open_id = "rotating-platform";
        }
        if (!mechanic_e2e_complete)
            fail_e2e("mechanic fixture could not open its reference graph");
        publish_editor.destination_parent = project_root.parent_path();
        publish_probe.enabled = true;
    }
    bool scene_e2e_complete = false;
    if (scene_e2e) {
        if (!session.has_map()) {
            fail_e2e("scene fixture map could not be opened");
        } else {
            const auto actor_position = session.map()->instances.empty()
                ? fabric::core::Vec2{}
                : session.map()->instances.front().transform.position;
            const bool trigger_authored =
                session.add_layer({"collision-e2e", "Collision E2E",
                    fabric::project::MapLayerKind::collision,
                    true, false, 0.0F}) &&
                session.add_layer({"triggers-e2e", "Triggers E2E",
                    fabric::project::MapLayerKind::triggers,
                    true, false, 0.0F}) &&
                session.add_collision_shape({
                    fabric::project::CollisionShapeKind::circle,
                    "collision-e2e", true, actor_position, 2.0F, 0.0F, {}}) &&
                session.declare_event(
                    {{.value = "scene-e2e-entered"}, {}}) &&
                session.add_trigger({
                    "scene-e2e-zone", "triggers-e2e", 0U,
                    {.value = "scene-e2e-entered"},
                    {{"source", std::string{"scene-studio"}}}}) &&
                session.save();
            const fabric::project::SceneDocument scene{
                .document = {.schema_version = 1, .type = "scene",
                             .id = {.value = "scene-studio-e2e"},
                             .name = "Scene Studio E2E"}};
            const auto destination = project_root.parent_path() /
                "scene-studio-e2e.scene-package";
            const bool authored = trigger_authored &&
                scene_session.create(project_root, scene) &&
                scene_session.add_map({{map_id, "map"}, "world"}) &&
                scene_session.set_entry_map(map_id) &&
                scene_session.add_transition(
                    {"loop", {{.value = "scene-studio-e2e"}, "scene"},
                     "start", std::nullopt}) &&
                scene_session.save();
            const auto published = authored
                ? fabric::project::publish_scene_package(
                      project_root, scene.document.id, destination)
                : fabric::project::ScenePackagePublishResult{};
            fabric::editor::SceneSession reloaded;
            fabric::editor::MapSession reloaded_map;
            fabric::runtime::SceneRuntimeSession published_runtime;
            const bool runtime_loaded = published.ok() &&
                published_runtime.load_package(destination);
            if (!authored || !published.ok() ||
                !reloaded.open(project_root, scene.document.id) ||
                !reloaded_map.open(project_root, map_id) ||
                !runtime_loaded || !published_runtime.scene() ||
                !published_runtime.map() ||
                published_runtime.scene()->document.id != scene.document.id ||
                published_runtime.map()->document.id != scene.document.id ||
                published_runtime.map()->collisions.size() != 1U ||
                published_runtime.map()->triggers.size() != 1U ||
                reloaded_map.map()->triggers.size() != 1U ||
                reloaded_map.map()->triggers.front().properties.size() != 1U ||
                reloaded.scene()->maps.size() != 1U ||
                reloaded.scene()->transitions.size() != 1U ||
                !std::filesystem::is_regular_file(
                    destination /
                    fabric::project::scene_package_manifest_filename)) {
                std::string detail =
                    "scene authoring, reload, publication or runtime load failed";
                for (const auto& error : published_runtime.errors()) {
                    detail += " | runtime: " + error;
                }
                fail_e2e(detail);
            } else {
                scene_e2e_complete = true;
                status = "Scene E2E authored, published and loaded by runtime";
            }
        }
    }
    bool transformation_e2e_complete = false;
    if (transformation_e2e) {
        if (!session.has_map()) {
            fail_e2e("transformation fixture map could not be opened");
        } else {
            fabric::project::EntityTransformation transformation;
            transformation.document.id = {.value = "map-transformation-e2e"};
            transformation.document.name = "Map Transformation E2E";
            transformation.source_entity = {
                {.value = "rotating-platform-entity"}, "entity"};
            transformation.destination_entity = {
                {.value = "textile-head-entity"}, "entity"};
            fabric::editor::TransformationSession transformation_session;
            const bool authored = transformation_session.create(
                    project_root, transformation) &&
                transformation_session.save();
            fabric::runtime::PreviewRuntime preview_runtime;
            const bool loaded = authored && preview_runtime.load({
                .project_root = project_root,
                .map_id = map_id,
                .mode = fabric::runtime::RuntimeMode::smoke_test,
                .trace = {.session_id = trace_session_id,
                          .resource_id = map_id.value},
                .log_output = &std::clog});
            const bool transformed = loaded && preview_runtime.transform_instance(
                "rotating-platform-instance", transformation.document.id);
            const auto destination = transformed
                ? preview_runtime.instance_entity_id("rotating-platform-instance")
                : std::nullopt;
            const auto source_instance = std::ranges::find(
                session.map()->instances, std::string{"rotating-platform-instance"},
                &fabric::project::MapInstance::id);
            transformation_e2e_complete = destination &&
                destination->value == "textile-head-entity" &&
                !preview_runtime.packet_order().empty() &&
                source_instance != session.map()->instances.end() &&
                source_instance->prefab &&
                source_instance->prefab->id.value == "rotating-platform-prefab" &&
                !session.dirty();
            if (!transformation_e2e_complete)
                fail_e2e("isolated transformation preview did not preserve the map");
        }
    }
    if (e2e_mode && *e2e_mode != CloseE2eMode::clean) {
        if (!session.has_map() || !session.manifest()) {
            fail_e2e("fixture map could not be opened");
        } else if (!session.declare_event({
                       {.value = "close-e2e-dirty"}, {}})) {
            fail_e2e("fixture could not be made dirty");
        } else {
            const auto autosave_start =
                fabric::editor::AutosaveScheduler::Clock::now();
            static_cast<void>(session.update_autosave(autosave_start));
            const auto autosave_status = session.update_autosave(
                autosave_start + std::chrono::seconds{31});
            if (autosave_status != fabric::editor::AutosaveStatus::saved)
                fail_e2e("autosave was not written");
            const auto document_path = fabric::project::map_document_path(
                *session.manifest(), map_id);
            e2e_primary_path = project_root / document_path;
            e2e_autosave_path = project_root /
                fabric::project::autosave_document_path(document_path);
            e2e_primary_contents = read_binary_file(e2e_primary_path);
            e2e_autosave_contents = read_binary_file(e2e_autosave_path);
            if (!e2e_primary_contents || !e2e_autosave_contents)
                fail_e2e("primary or autosave could not be read");
        }
    }
    std::string event_id;
    std::string selected_event_id;
    int event_editor_index = -1;
    std::vector<fabric::project::MapProperty> event_payload_editor;
    std::string event_property_id;
    std::string event_property_value;
    int event_property_kind = 2;
    int event_resource_kind = 3;
    std::string trigger_id;
    std::string trigger_event_id;
    int trigger_collision_index = 0;
    int selected_collision_index = -1;
    int remove_collision_request = -1;
    int collision_editor_index = -1;
    fabric::project::CollisionShape collision_editor;
    std::string new_collision_layer;
    int new_collision_kind = 0;
    bool new_collision_sensor = true;
    fabric::core::Vec2 new_collision_center{};
    float new_collision_radius = 1.0F;
    float new_collision_length = 2.0F;
    int selected_trigger_index = -1;
    std::string remove_event_request;
    std::string remove_trigger_request;
    int trigger_editor_index = -1;
    int trigger_editor_collision_index = 0;
    fabric::project::TriggerDefinition trigger_editor;
    std::string trigger_property_id;
    std::string trigger_property_value;
    int trigger_property_kind = 2;
    int trigger_resource_kind = 3;
    std::vector<std::string> selected_instances = mechanic_e2e
        ? std::vector<std::string>{"rotating-platform-instance"}
        : std::vector<std::string>{};
    bool focus_path_follower_panel = false;
    bool create_path_animation_request = false;
    std::string active_layer_id = placement_e2e ? "instances" : "";
    std::string new_layer_id;
    std::string new_layer_name;
    int new_layer_kind = 1;
    bool placement_mode = false;
    bool keep_placement_active = true;
    std::string placement_id;
    std::string placement_resource_id = placement_e2e
        ? "textile-head-entity" : "";
    int placement_kind = 0;
    std::string selected_prefab;
    std::string new_prefab_id;
    std::string new_prefab_entity;
    std::string new_prefab_mechanic;
    std::string override_id;
    std::string override_value;
    int override_kind = 2;
    int override_resource_kind = 3;
    std::string mechanic_override_parameter;
    std::string mechanic_override_value;
    std::string instance_property_id;
    std::string instance_property_value;
    std::string instance_property_filter;
    int instance_property_kind = 2;
    int instance_resource_kind = 3;
    std::string path_follower_id;
    float path_follower_progress = 0.0F;
    float path_follower_speed = 0.0F;
    bool path_follower_loop = true;
    bool path_follower_orient = true;
    float path_follower_rotation_offset = 0.0F;
    std::string path_follower_bound_instance;
    std::string transformation_preview_id;
    std::string transformation_preview_result;
    ImVec2 canvas_pan{0.0F, 0.0F};
    float canvas_zoom = 1.0F;
    bool canvas_grid_visible = true;
    CanvasGizmoState canvas_gizmo;
    MapMechanicOverlayState map_mechanic_gizmo;
    std::string requested_mechanic_node;
    CollisionPointGizmoState collision_point_gizmo;
    SelectionBoxState selection_box;
    MapPlacementProbe placement_probe{.enabled = placement_e2e || mechanic_e2e};
    std::size_t placement_e2e_frame = 0U;
    bool placement_context_observed = false;
    fabric::editor::MapSnapSettings canvas_snapping;
    TransformEditorState transform_editor;
    static_cast<void>(actions.register_action({
        .id = std::string{fabric::editor::editor_action_ids::configure_path_follower},
        .label = "Configure PathFollower",
        .shortcut = "",
        .availability = [&] {
            return fabric::editor::EditorActionAvailability{
                .enabled = selected_instances.size() == 1U,
                .disabled_reason = "Select one map instance before configuring its path follower.",
            };
        },
        .execute = [&] {
            focus_path_follower_panel = true;
            status = "PathFollower inspector focused";
            return true;
        },
    }));
    static_cast<void>(actions.register_action({
        .id = std::string{fabric::editor::editor_action_ids::create_path_animation},
        .label = "Create path animation",
        .shortcut = "",
        .availability = [&] {
            if (selected_instances.size() != 1U)
                return fabric::editor::EditorActionAvailability{
                    .enabled = false,
                    .disabled_reason = "Select one map instance before creating a path animation."};
            if (!session.map())
                return fabric::editor::EditorActionAvailability{
                    .enabled = false,
                    .disabled_reason = "Open a map before creating a path animation."};
            const auto instance = std::ranges::find(
                session.map()->instances, selected_instances.front(),
                &fabric::project::MapInstance::id);
            if (instance == session.map()->instances.end() ||
                (!instance->entity && !instance->prefab))
                return fabric::editor::EditorActionAvailability{
                    .enabled = false,
                    .disabled_reason = "The selected instance must reference an entity or prefab."};
            if (!instance->path_follower || instance->path_follower->path.id.value.empty())
                return fabric::editor::EditorActionAvailability{
                    .enabled = false,
                    .disabled_reason = "Configure a valid PathFollower before creating its animation."};
            return fabric::editor::EditorActionAvailability{};
        },
        .execute = [&] {
            create_path_animation_request = true;
            focus_path_follower_panel = true;
            status = "Path animation workflow focused";
            return true;
        },
    }));
    float preview_time = 0.0F;
    bool preview_playing = true;
    float& layers_pane_width = layout.primary_panel_width;
    float& selection_pane_width = layout.secondary_panel_width;
    fabric::render::MapPreviewResult map_preview;
    MapPreviewRenderer preview_render_state{
        .renderer = &map_renderer,
        .textures = &map_textures,
    };
    if (session.map()) {
        static_cast<void>(editor_context.open_document(
            session.map()->document.id, fabric::editor::EditorWorkspace::map));
        static_cast<void>(editor_context.set_view({
            .zoom = canvas_zoom,
            .pan = {canvas_pan.x, canvas_pan.y},
            .playhead = preview_time,
            .active_tool = "select",
            .active_panel = "inspector",
        }));
    }
    std::string known_scene_document;
    std::string known_mechanic_document =
        mechanic_e2e && mechanic_session.graph()
        ? mechanic_session.graph()->document.id.value
        : std::string{};
    bool command_palette_open = ui_accessibility_test;
    bool command_palette_rendered = false;
    bool running = true;
    const auto push_mouse_position = [&](const ImVec2 target) {
        SDL_Event motion{};
        motion.type = SDL_MOUSEMOTION;
        motion.motion.windowID = SDL_GetWindowID(window);
        motion.motion.x = static_cast<int>(std::lround(target.x));
        motion.motion.y = static_cast<int>(std::lround(target.y));
        static_cast<void>(SDL_PushEvent(&motion));
    };
    const auto push_left_button = [&](const Uint32 type, const ImVec2 target) {
        SDL_Event button{};
        button.type = type;
        button.button.button = SDL_BUTTON_LEFT;
        button.button.windowID = SDL_GetWindowID(window);
        button.button.x = static_cast<int>(std::lround(target.x));
        button.button.y = static_cast<int>(std::lround(target.y));
        static_cast<void>(SDL_PushEvent(&button));
    };
    const auto push_left_click = [&](const ImVec2 target) {
        push_mouse_position(target);
        push_left_button(SDL_MOUSEBUTTONDOWN, target);
        push_left_button(SDL_MOUSEBUTTONUP, target);
    };
    while (running) {
        if (placement_e2e) {
            std::optional<ImVec2> target;
            if (placement_e2e_frame == 2U &&
                placement_probe.placement_button_seen) {
                target = placement_probe.placement_button_screen;
            } else if ((placement_e2e_frame == 6U ||
                        placement_e2e_frame == 10U) &&
                       placement_mode && placement_probe.canvas_seen) {
                target = placement_probe.canvas_center;
                target->x += placement_e2e_frame == 6U ? -72.0F : 72.0F;
            }
            if (target) {
                SDL_Event motion{};
                motion.type = SDL_MOUSEMOTION;
                motion.motion.windowID = SDL_GetWindowID(window);
                motion.motion.x = static_cast<int>(std::lround(target->x));
                motion.motion.y = static_cast<int>(std::lround(target->y));
                static_cast<void>(SDL_PushEvent(&motion));
                for (const auto type : {SDL_MOUSEBUTTONDOWN,
                                        SDL_MOUSEBUTTONUP}) {
                    SDL_Event button{};
                    button.type = type;
                    button.button.button = SDL_BUTTON_LEFT;
                    button.button.windowID = SDL_GetWindowID(window);
                    button.button.x = motion.motion.x;
                    button.button.y = motion.motion.y;
                    static_cast<void>(SDL_PushEvent(&button));
                }
            }
            if (placement_e2e_frame == 12U &&
                placement_context_observed) {
                for (const auto type : {SDL_KEYDOWN, SDL_KEYUP}) {
                    SDL_Event key{};
                    key.type = type;
                    key.key.windowID = SDL_GetWindowID(window);
                    key.key.keysym.sym = SDLK_ESCAPE;
                    key.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                    static_cast<void>(SDL_PushEvent(&key));
                }
            }
        }
        if (mechanic_e2e && mechanic_e2e_frame == 2U &&
            mechanic_probe.instance_action_seen) {
            push_left_click(mechanic_probe.instance_action_screen);
        }
        if (mechanic_e2e &&
            (mechanic_e2e_frame == 5U || mechanic_e2e_frame == 7U) &&
            mechanic_probe.source_seen && mechanic_probe.target_seen) {
            const ImVec2 target = mechanic_e2e_frame == 5U
                ? mechanic_probe.source_screen
                : mechanic_probe.target_screen;
            push_left_click(target);
        }
        if (mechanic_e2e && mechanic_probe.spatial_handle_seen &&
            mechanic_e2e_frame >= 9U && mechanic_e2e_frame <= 11U) {
            ImVec2 target = mechanic_probe.spatial_handle_screen;
            if (mechanic_e2e_frame >= 10U) target.x += 32.0F;
            push_mouse_position(target);
            if (mechanic_e2e_frame == 9U || mechanic_e2e_frame == 11U) {
                push_left_button(mechanic_e2e_frame == 9U
                                     ? SDL_MOUSEBUTTONDOWN
                                     : SDL_MOUSEBUTTONUP,
                                 target);
            }
        }
        if (mechanic_e2e && mechanic_probe.body_handle_seen &&
            mechanic_e2e_frame == 13U) {
            push_left_click(mechanic_probe.body_handle_screen);
        }
        if (mechanic_e2e && mechanic_probe.resize_handle_seen &&
            mechanic_e2e_frame >= 15U && mechanic_e2e_frame <= 17U) {
            if (!mechanic_resize_drag_origin)
                mechanic_resize_drag_origin = mechanic_probe.resize_handle_screen;
            ImVec2 target = *mechanic_resize_drag_origin;
            if (mechanic_e2e_frame >= 16U) {
                target.x += 32.0F;
                target.y -= 16.0F;
            }
            push_mouse_position(target);
            if (mechanic_e2e_frame == 15U || mechanic_e2e_frame == 17U) {
                push_left_button(mechanic_e2e_frame == 15U
                                     ? SDL_MOUSEBUTTONDOWN
                                     : SDL_MOUSEBUTTONUP,
                                 target);
            }
        }
        if (mechanic_e2e && mechanic_probe.rotation_handle_seen &&
            mechanic_e2e_frame >= 19U && mechanic_e2e_frame <= 21U) {
            if (!mechanic_rotation_drag_origin)
                mechanic_rotation_drag_origin = mechanic_probe.rotation_handle_screen;
            ImVec2 target = *mechanic_rotation_drag_origin;
            if (mechanic_e2e_frame >= 20U) {
                target.x += 24.0F;
                target.y += 12.0F;
            }
            push_mouse_position(target);
            if (mechanic_e2e_frame == 19U || mechanic_e2e_frame == 21U) {
                push_left_button(mechanic_e2e_frame == 19U
                                     ? SDL_MOUSEBUTTONDOWN
                                     : SDL_MOUSEBUTTONUP,
                                 target);
            }
        }
        if (mechanic_e2e && mechanic_probe.joint_handle_seen &&
            mechanic_e2e_frame >= 23U && mechanic_e2e_frame <= 25U) {
            if (!mechanic_joint_drag_origin)
                mechanic_joint_drag_origin = mechanic_probe.joint_handle_screen;
            ImVec2 target = *mechanic_joint_drag_origin;
            if (mechanic_e2e_frame >= 24U) target.x += 32.0F;
            push_mouse_position(target);
            if (mechanic_e2e_frame == 23U || mechanic_e2e_frame == 25U) {
                push_left_button(mechanic_e2e_frame == 23U
                                     ? SDL_MOUSEBUTTONDOWN
                                     : SDL_MOUSEBUTTONUP,
                                 target);
            }
        }
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::map &&
            mechanic_map_e2e_frame == 2U &&
            placement_probe.frame_selection_seen) {
            push_left_click(placement_probe.frame_selection_screen);
        }
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::map &&
            mechanic_map_e2e_frame == 4U &&
            placement_probe.mechanic.parameter_body_seen) {
            push_left_click(placement_probe.mechanic.parameter_body_screen);
        }
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::map &&
            placement_probe.mechanic.parameter_handle_seen &&
            mechanic_map_e2e_frame >= 6U && mechanic_map_e2e_frame <= 8U) {
            if (!mechanic_map_resize_drag_origin)
                mechanic_map_resize_drag_origin =
                    placement_probe.mechanic.parameter_handle_screen;
            ImVec2 target = *mechanic_map_resize_drag_origin;
            if (mechanic_map_e2e_frame >= 7U) {
                target.x += 8.0F;
                target.y -= 4.0F;
            }
            push_mouse_position(target);
            if (mechanic_map_e2e_frame == 6U || mechanic_map_e2e_frame == 8U) {
                push_left_button(mechanic_map_e2e_frame == 6U
                                     ? SDL_MOUSEBUTTONDOWN
                                     : SDL_MOUSEBUTTONUP,
                                 target);
            }
        }
        if (mechanic_e2e && mechanic_map_parameter_verified &&
            active_workspace == ActiveWorkspace::map &&
            (mechanic_map_e2e_frame == 10U ||
             mechanic_map_e2e_frame == 11U) &&
            placement_probe.mechanic.parameter_body_seen) {
            push_left_click(placement_probe.mechanic.parameter_body_screen);
        }
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::publish &&
            ((publish_e2e_frame >= 2U && publish_e2e_frame <= 3U &&
              publish_probe.validate_seen) ||
             (publish_e2e_frame >= 5U && publish_e2e_frame <= 6U &&
              publish_probe.publish_seen))) {
            const bool validate = publish_e2e_frame <= 3U;
            const ImVec2 target = validate ? publish_probe.validate_screen
                                           : publish_probe.publish_screen;
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.x = static_cast<int>(std::lround(target.x));
            motion.motion.y = static_cast<int>(std::lround(target.y));
            static_cast<void>(SDL_PushEvent(&motion));
            SDL_Event button{};
            button.type = publish_e2e_frame == 2U || publish_e2e_frame == 5U
                ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            button.button.button = SDL_BUTTON_LEFT;
            button.button.windowID = SDL_GetWindowID(window);
            button.button.x = motion.motion.x;
            button.button.y = motion.motion.y;
            static_cast<void>(SDL_PushEvent(&button));
        }
        if (e2e_mode && !e2e_failed && !e2e_event_injected) {
            SDL_Event close_event{};
            if (*e2e_mode == CloseE2eMode::window) {
                close_event.type = SDL_WINDOWEVENT;
                close_event.window.event = SDL_WINDOWEVENT_CLOSE;
                close_event.window.windowID = SDL_GetWindowID(window);
            } else {
                close_event.type = SDL_QUIT;
            }
            if (SDL_PushEvent(&close_event) < 0)
                fail_e2e("close event could not be injected");
            e2e_event_injected = true;
        }
        if (e2e_mode && e2e_failed) running = false;
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                transition_guard.request(
                    fabric::editor::SessionAction::quit,
                    session.dirty() || mechanic_session.dirty() ||
                        scene_session.dirty());
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const auto* active_document = editor_context.active_document();
        if (active_document != nullptr &&
            active_document->workspace == fabric::editor::EditorWorkspace::map) {
            static_cast<void>(editor_context.set_view({
                .zoom = canvas_zoom,
                .pan = {canvas_pan.x, canvas_pan.y},
                .playhead = preview_time,
                .active_tool = placement_mode ? "place" : "select",
                .active_panel = "inspector",
            }));
        }
        if (scene_session.scene() &&
            known_scene_document != scene_session.scene()->document.id.value) {
            known_scene_document = scene_session.scene()->document.id.value;
            static_cast<void>(editor_context.open_document(
                scene_session.scene()->document.id,
                fabric::editor::EditorWorkspace::scene));
        }
        if (mechanic_session.graph() &&
            known_mechanic_document !=
                mechanic_session.graph()->document.id.value) {
            known_mechanic_document =
                mechanic_session.graph()->document.id.value;
            static_cast<void>(editor_context.open_document(
                mechanic_session.graph()->document.id,
                fabric::editor::EditorWorkspace::logic));
        }
        const auto& imgui_io_frame = ImGui::GetIO();
#if defined(__APPLE__)
        const bool command_modifier = imgui_io_frame.KeySuper;
#else
        const bool command_modifier = imgui_io_frame.KeyCtrl;
#endif
        const bool shortcuts_enabled = !imgui_io_frame.WantTextInput &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_S, false))
            static_cast<void>(actions.invoke(
                fabric::editor::editor_action_ids::save));
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            static_cast<void>(actions.invoke(
                fabric::editor::editor_action_ids::undo));
        }
        if (shortcuts_enabled && command_modifier &&
            (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
             (imgui_io_frame.KeyShift &&
              ImGui::IsKeyPressed(ImGuiKey_Z, false)))) {
            static_cast<void>(actions.invoke(
                fabric::editor::editor_action_ids::redo));
        }
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_Q, false))
            transition_guard.request(
                fabric::editor::SessionAction::quit,
                session.dirty() || mechanic_session.dirty() || scene_session.dirty());
        if (shortcuts_enabled && command_modifier && imgui_io_frame.KeyShift &&
            ImGui::IsKeyPressed(ImGuiKey_P, false)) {
            command_palette_open = true;
        }
        if (session.update_autosave() == fabric::editor::AutosaveStatus::failed)
            status = "Map autosave failed";
        if (mechanic_session.update_autosave() ==
            fabric::editor::AutosaveStatus::failed)
            status = "Mechanic autosave failed";
        if (scene_session.update_autosave() ==
            fabric::editor::AutosaveStatus::failed)
            status = "Scene autosave failed";
        if (mechanic_session.simulation().playing())
            static_cast<void>(mechanic_session.update_preview(ImGui::GetIO().DeltaTime));
        ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Map Studio", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                const auto save_availability = actions.availability(
                    fabric::editor::editor_action_ids::save);
                if (ImGui::MenuItem("Save", "Ctrl+S", false,
                                    save_availability.enabled)) {
                    static_cast<void>(actions.invoke(
                        fabric::editor::editor_action_ids::save));
                }
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                    transition_guard.request(
                        fabric::editor::SessionAction::quit,
                        session.dirty() || mechanic_session.dirty() ||
                            scene_session.dirty());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                const auto undo_availability = actions.availability(
                    fabric::editor::editor_action_ids::undo);
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                                    undo_availability.enabled)) {
                    static_cast<void>(actions.invoke(
                        fabric::editor::editor_action_ids::undo));
                }
                const auto redo_availability = actions.availability(
                    fabric::editor::editor_action_ids::redo);
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
                                    redo_availability.enabled)) {
                    static_cast<void>(actions.invoke(
                        fabric::editor::editor_action_ids::redo));
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Command Palette...", "Ctrl+Shift+P"))
                    command_palette_open = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::TextUnformatted("F: frame selection · Home: frame map");
                ImGui::TextUnformatted("Ctrl+D: duplicate · arrows: nudge · Delete: remove");
                ImGui::TextUnformatted("Ctrl+S: save · Ctrl+Z/Y: undo/redo · Ctrl+Q: quit");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem(
                        "Automatic layout", nullptr,
                        layout.mode ==
                            fabric::editor::EditorLayoutMode::automatic)) {
                    layout = {
                        .mode = fabric::editor::EditorLayoutMode::automatic,
                        .primary_panel_width = 260.0F,
                        .secondary_panel_width = 340.0F,
                    };
                }
                if (ImGui::MenuItem(
                        "Compact layout", nullptr,
                        layout.mode ==
                            fabric::editor::EditorLayoutMode::compact)) {
                    layout = {
                        .mode = fabric::editor::EditorLayoutMode::compact,
                        .primary_panel_width = 220.0F,
                        .secondary_panel_width = 300.0F,
                        .task_panel_height = 190.0F,
                    };
                }
                if (ImGui::MenuItem(
                        "Wide layout", nullptr,
                        layout.mode == fabric::editor::EditorLayoutMode::wide)) {
                    layout = {
                        .mode = fabric::editor::EditorLayoutMode::wide,
                        .primary_panel_width = 310.0F,
                        .secondary_panel_width = 390.0F,
                        .task_panel_height = 300.0F,
                    };
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        command_palette_rendered =
            draw_command_palette(actions, command_palette_open) ||
            command_palette_rendered;
        if (!editor_context.open_documents().empty()) {
            static_cast<void>(draw_document_navigation(
                editor_context,
                [&](const fabric::core::ResourceId& id) {
                    if (session.map() && session.map()->document.id == id)
                        return session.map()->document.name;
                    if (scene_session.scene() &&
                        scene_session.scene()->document.id == id)
                        return scene_session.scene()->document.name;
                    if (mechanic_session.graph() &&
                        mechanic_session.graph()->document.id == id)
                        return mechanic_session.graph()->document.name;
                    const auto resource = std::ranges::find(
                        resource_catalog.resources(), id,
                        &fabric::editor::StudioResource::id);
                    return resource == resource_catalog.resources().end()
                        ? id.value
                        : resource->name;
                },
                [&](const fabric::editor::EditorDocumentState& document) {
                    if (document.workspace ==
                        fabric::editor::EditorWorkspace::map) {
                        canvas_zoom = document.view.zoom;
                        canvas_pan = {document.view.pan.x, document.view.pan.y};
                        preview_time = document.view.playhead;
                        placement_mode = document.view.active_tool == "place";
                        selected_instances.clear();
                        if (session.map()) {
                            std::vector<fabric::core::ResourceId> instance_ids;
                            instance_ids.reserve(session.map()->instances.size());
                            for (const auto& instance : session.map()->instances)
                                instance_ids.push_back({.value = instance.id});
                            const auto resolved =
                                editor_context.resolve_selection(instance_ids);
                            selected_instances.reserve(resolved.indices.size());
                            for (const auto index : resolved.indices)
                                selected_instances.push_back(
                                    session.map()->instances[index].id);
                        }
                        active_workspace = ActiveWorkspace::map;
                        return true;
                    }
                    if (document.workspace ==
                            fabric::editor::EditorWorkspace::scene &&
                        scene_session.scene() &&
                        scene_session.scene()->document.id == document.id) {
                        active_workspace = ActiveWorkspace::scene;
                        return true;
                    }
                    if (document.workspace ==
                            fabric::editor::EditorWorkspace::logic &&
                        mechanic_session.graph() &&
                        mechanic_session.graph()->document.id == document.id) {
                        active_workspace = ActiveWorkspace::mechanic;
                        return true;
                    }
                    return false;
                }));
            ImGui::Separator();
        }
        if (ImGui::RadioButton(
                "Map", active_workspace == ActiveWorkspace::map))
            active_workspace = ActiveWorkspace::map;
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Scene", active_workspace == ActiveWorkspace::scene))
            active_workspace = ActiveWorkspace::scene;
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Mechanics", active_workspace == ActiveWorkspace::mechanic))
            active_workspace = ActiveWorkspace::mechanic;
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Publish", active_workspace == ActiveWorkspace::publish))
            active_workspace = ActiveWorkspace::publish;
        ImGui::Separator();
        if (active_workspace == ActiveWorkspace::map) {
        if (!session.has_map()) {
            const float start_width = std::min(640.0F, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX(std::max(
                ImGui::GetCursorPosX(),
                (ImGui::GetWindowContentRegionMax().x - start_width) * 0.5F));
            ImGui::BeginChild("map-start-workspace", {start_width, 0.0F}, false);
            ImGui::TextUnformatted("Map Studio");
            ImGui::TextDisabled("Open a map or create one from a visible name.");
            const auto manifest = fabric::project::load_manifest(project_root);
            if (manifest.ok()) {
                ImGui::SeparatorText("Open an existing map");
                const auto maps_directory = project_root / manifest.manifest->directories.maps;
                draw_resource_picker("Open map:", maps_directory, ".map.json",
                                     open_map_id, &resource_catalog);
                ImGui::BeginDisabled(open_map_id.empty());
                if (ImGui::Button("Open selected", {-1.0F, 0.0F})) {
                    status = session.open(project_root, {.value = open_map_id})
                        ? "Map opened" : "Map could not be opened";
                }
                ImGui::EndDisabled();
                draw_disabled_reason(open_map_id.empty(),
                                     "Choose an existing map first.");
                ImGui::SeparatorText("Create a new map");
                ImGui::SetNextItemWidth(-1.0F);
                ImGui::InputText("Visible name", &new_map_name);
                focus_first_field_error(session.errors(), "name", "map-create");
                draw_field_errors(session.errors(), "name",
                                  "Enter a visible non-empty map name.");
                new_map_id = fabric::editor::generated_resource_id(
                    new_map_name, "map").value;
                ImGui::TextDisabled("File id: %s", new_map_id.c_str());
                ImGui::BeginDisabled(new_map_name.empty());
                if (ImGui::Button("Create and open map", {-1.0F, 0.0F})) {
                    const fabric::project::MapDocument map{
                        .document = {.schema_version = 1, .type = "map",
                                     .id = {.value = new_map_id}, .name = new_map_name}};
                    const bool created = session.create(project_root, map);
                    status = created ? "Map created" : "Map creation failed";
                    if (created)
                        static_cast<void>(resource_catalog.refresh_resources());
                }
                ImGui::EndDisabled();
                draw_disabled_reason(new_map_name.empty(),
                                     "Enter a visible map name first.");
            }
            draw_validation_errors(session.errors());
            ImGui::EndChild();
        } else {
            if (session.has_recovery()) {
                ImGui::TextColored({1.0F, 0.75F, 0.25F, 1.0F},
                                   "A newer valid map autosave is available.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Recover")) {
                    status = session.accept_recovery()
                        ? "Map autosave recovered; save to publish it"
                        : "Map recovery failed";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Dismiss recovery")) {
                    session.decline_recovery();
                    status = "Map recovery dismissed";
                }
            }
            const auto& map = *session.map();
            if (active_layer_id.empty() && !map.layers.empty())
                active_layer_id = map.layers.front().id;
            ImGui::Text("Map: %s (%s)", map.document.name.c_str(),
                        map.document.id.value.c_str());
            ImGui::SameLine();
            ImGui::TextColored(session.dirty() ? ImVec4{1.0F, 0.75F, 0.25F, 1.0F}
                                               : ImVec4{0.45F, 0.9F, 0.55F, 1.0F},
                               session.dirty() ? "dirty" : "saved");
            if (ImGui::Button("Save")) {
                static_cast<void>(actions.invoke(
                    fabric::editor::editor_action_ids::save));
            }
            ImGui::SameLine();
            const bool renderer_blocked = !map_renderer.ready() ||
                !preview_render_state.errors.empty();
            const std::string renderer_block_reason = !map_renderer.ready()
                ? "Preview renderer unavailable: " + map_renderer.initialization_error()
                : (!preview_render_state.errors.empty()
                    ? "Resolve render diagnostics before preview or publication."
                    : std::string{});
            ImGui::BeginDisabled(renderer_blocked);
            if (ImGui::Button("Preview")) {
                preview_time = 0.0F;
                preview_playing = true;
                status = "Map preview restarted";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(renderer_blocked, renderer_block_reason);
            ImGui::SameLine();
            const auto undo_availability = actions.availability(
                fabric::editor::editor_action_ids::undo);
            ImGui::BeginDisabled(!undo_availability.enabled);
            if (ImGui::Button("Undo")) {
                static_cast<void>(actions.invoke(
                    fabric::editor::editor_action_ids::undo));
            }
            ImGui::EndDisabled();
            draw_disabled_reason(!undo_availability.enabled,
                                 undo_availability.disabled_reason);
            ImGui::SameLine();
            const auto redo_availability = actions.availability(
                fabric::editor::editor_action_ids::redo);
            ImGui::BeginDisabled(!redo_availability.enabled);
            if (ImGui::Button("Redo")) {
                static_cast<void>(actions.invoke(
                    fabric::editor::editor_action_ids::redo));
            }
            ImGui::EndDisabled();
            draw_disabled_reason(!redo_availability.enabled,
                                 redo_availability.disabled_reason);
            ImGui::Separator();
            const auto available_width = ImGui::GetContentRegionAvail().x;
            layers_pane_width = std::clamp(
                layers_pane_width, 220.0F,
                std::max(220.0F,
                         available_width - selection_pane_width - 336.0F));
            selection_pane_width = std::clamp(
                selection_pane_width, 300.0F,
                std::max(300.0F,
                         available_width - layers_pane_width - 336.0F));
            const float canvas_pane_width = std::max(
                320.0F,
                available_width - layers_pane_width - selection_pane_width - 16.0F);
            ImGui::BeginChild("map-layers-pane",
                              ImVec2{layers_pane_width, 0.0F}, true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ui_map_workspace_seen = true;
            ui_map_layers_x = ImGui::GetWindowPos().x;
            const bool layers_open = ImGui::CollapsingHeader(
                ("Layers (" + std::to_string(map.layers.size()) + ")##map-layers-tree").c_str(),
                ImGuiTreeNodeFlags_DefaultOpen);
            if (layers_open) {
            ImGui::SetNextItemWidth(120.0F);
            ImGui::InputText("Layer id", &new_layer_id);
            focus_first_field_error(session.errors(), "id", "layer-create");
            ImGui::SameLine();
            draw_resource_name_field("Layer name", new_layer_name, 140.0F);
            focus_first_field_error(session.errors(), "name", "layer-create");
            ImGui::SetNextItemWidth(150.0F);
            ImGui::Combo("Layer kind", &new_layer_kind,
                         "visual\0instances\0collision\0triggers\0");
            ImGui::SameLine();
            ImGui::BeginDisabled(new_layer_id.empty() || new_layer_name.empty());
            if (ImGui::Button("Add layer")) {
                const auto added = session.add_layer({
                    new_layer_id, new_layer_name,
                    std::array{fabric::project::MapLayerKind::visual,
                               fabric::project::MapLayerKind::instances,
                               fabric::project::MapLayerKind::collision,
                               fabric::project::MapLayerKind::triggers}[static_cast<std::size_t>(new_layer_kind)],
                    true, false, 0.0F});
                status = added ? "Layer added" : "Layer creation rejected";
                if (added) {
                    active_layer_id = new_layer_id;
                    new_layer_id.clear();
                    new_layer_name.clear();
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(new_layer_id.empty() || new_layer_name.empty(),
                                 "Enter both a layer id and a layer name.");
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
                if (ImGui::SmallButton(active_layer_id == layer.id ? "Active" : "Use"))
                    active_layer_id = layer.id;
                ImGui::SameLine();
                float depth = layer.depth;
                ImGui::SetNextItemWidth(90.0F);
                if (ImGui::DragFloat("Depth (world units)##depth", &depth, 0.1F) &&
                    ImGui::IsItemDeactivatedAfterEdit() &&
                    session.set_layer_depth({.value = layer.id}, depth)) {
                    status = "Layer depth changed";
                    layer_changed = true;
                }
                ImGui::PopID();
                if (layer_changed) break;
            }
            if (layer_changed) ImGui::TextDisabled("Layer edit recorded in undo history");
            }
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
                const fabric::core::ResourceId selected_id{selected_instances.front()};
                const auto contextual_mechanic = instance_mechanic_id(
                    map, selected_id.value);
                if (contextual_mechanic) {
                    if (ImGui::Button("Edit instance mechanic")) {
                        static_cast<void>(editor_context.navigate(
                            map.document.id,
                            fabric::editor::EditorWorkspace::map,
                            selected_id));
                        const bool opened = mechanic_session.open_prefab_instance(
                            session.project_root(), map, selected_id);
                        status = opened
                            ? "Instance mechanic opened"
                            : "Instance mechanic could not be opened";
                        if (opened) {
                            mechanic_editor.open_id =
                                contextual_mechanic->value;
                            static_cast<void>(editor_context.open_document(
                                *contextual_mechanic,
                                fabric::editor::EditorWorkspace::logic));
                            active_workspace = ActiveWorkspace::mechanic;
                            if (mechanic_probe.enabled)
                                mechanic_probe.instance_action_clicked = true;
                        }
                    }
                    if (mechanic_probe.enabled) {
                        const auto minimum = ImGui::GetItemRectMin();
                        const auto maximum = ImGui::GetItemRectMax();
                        mechanic_probe.instance_action_screen = {
                            (minimum.x + maximum.x) * 0.5F,
                            (minimum.y + maximum.y) * 0.5F};
                        mechanic_probe.instance_action_seen = true;
                    }
                }
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
            draw_disabled_reason(selected_instances.empty(),
                                 "Select at least one instance first.");
            ImGui::BeginDisabled(selected_instances.empty() || active_layer_id.empty());
            if (ImGui::Button("Move selected to active layer")) {
                std::vector<fabric::core::ResourceId> ids;
                for (const auto& id : selected_instances) ids.push_back({.value = id});
                status = session.set_instances_layer(ids, {.value = active_layer_id})
                    ? "Instances moved to active layer"
                    : "Layer move rejected (locked or invalid)";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(selected_instances.empty() || active_layer_id.empty(),
                                 "Select an instance and an active layer first.");
            ImGui::EndChild();
            ImGui::SameLine(0.0F, 2.0F);
            ImGui::InvisibleButton("##map-layers-splitter", {6.0F, -1.0F});
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                if (ImGui::IsItemActive())
                    layers_pane_width = std::clamp(
                        layers_pane_width + ImGui::GetIO().MouseDelta.x,
                        220.0F,
                        std::max(220.0F,
                                 available_width - selection_pane_width - 336.0F));
            }
            ImGui::SameLine(0.0F, 2.0F);
            ImGui::BeginChild("map-canvas-pane", {canvas_pane_width, 0.0F}, true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ui_map_canvas_x = ImGui::GetWindowPos().x;
            ui_map_canvas_width = ImGui::GetWindowSize().x;
            ImGui::SeparatorText("Placement");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Instance id (auto)", &placement_id);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::Combo("Resource kind", &placement_kind, "entity\0prefab\0");
            if (placement_kind == 0 && session.manifest()) {
                const auto directory = session.project_root() /
                    session.manifest()->directories.entities;
                draw_resource_picker("Entity resources:", directory, ".entity.json",
                                     placement_resource_id, &resource_catalog);
            } else {
                std::vector<std::string> prefab_ids;
                prefab_ids.reserve(map.prefabs.size());
                for (const auto& prefab : map.prefabs) prefab_ids.push_back(prefab.id);
                draw_id_picker("Prefab resources", prefab_ids, placement_resource_id,
                               "Choose a prefab...");
            }
            if (!placement_resource_id.empty() && placement_id.empty()) {
                const auto base = fabric::editor::generated_resource_id(
                    placement_resource_id + " instance", "instance").value;
                placement_id = base;
                for (std::size_t suffix = 2U; std::ranges::any_of(
                         map.instances, [&](const auto& instance) {
                             return instance.id == placement_id;
                         }); ++suffix)
                    placement_id = base + "-" + std::to_string(suffix);
            }
            ImGui::BeginDisabled(placement_id.empty() || placement_resource_id.empty() ||
                                 active_layer_id.empty());
            if (ImGui::Button(placement_mode ? "Cancel placement" : "Place in canvas"))
                placement_mode = !placement_mode;
            if (placement_probe.enabled) {
                placement_probe.placement_button_seen = true;
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                placement_probe.placement_button_screen = {
                    (minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F};
            }
            ImGui::EndDisabled();
            draw_disabled_reason(placement_id.empty() || placement_resource_id.empty() ||
                                     active_layer_id.empty(),
                                 "Enter an instance id, choose a resource and select an active layer.");
            ImGui::SameLine();
            ImGui::Checkbox("Keep placing", &keep_placement_active);
            draw_technical_tooltip(
                "Keep this resource, layer and snapping active after each placement. "
                "A unique instance id is generated for every click; Escape stops the tool.");
            ImGui::Checkbox("Play visual animation", &preview_playing);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SliderFloat("Preview time (seconds)", &preview_time, 0.0F, 10.0F, "%.2f s");
            draw_technical_tooltip("Time used to evaluate map animations and preview state.");
            if (preview_playing) preview_time += ImGui::GetIO().DeltaTime;
            map_preview = fabric::render::resolve_map_preview(
                session.project_root(), *session.manifest(), map, preview_time);
            preview_render_state.preview = &map_preview;
            preview_render_state.project_root = &session.project_root();
            preview_render_state.manifest = &*session.manifest();
            draw_map_canvas(session, selected_instances, canvas_pan, canvas_zoom,
                            canvas_grid_visible, canvas_gizmo,
                            selected_collision_index, collision_point_gizmo,
                            selected_trigger_index, active_layer_id, selection_box,
                            placement_mode, keep_placement_active,
                            placement_id, placement_resource_id, placement_kind,
                            canvas_snapping, preview_render_state,
                            mechanic_session, map_mechanic_gizmo,
                            requested_mechanic_node, status,
                            &placement_probe);
            if (!requested_mechanic_node.empty() && mechanic_session.graph() &&
                selected_instances.size() == 1U) {
                static_cast<void>(editor_context.navigate(
                    map.document.id, fabric::editor::EditorWorkspace::map,
                    fabric::core::ResourceId{
                        .value = selected_instances.front()}));
                mechanic_editor.selected_node = requested_mechanic_node;
                static_cast<void>(editor_context.open_document(
                    mechanic_session.graph()->document.id,
                    fabric::editor::EditorWorkspace::logic));
                active_workspace = ActiveWorkspace::mechanic;
                status = "Mechanic node opened from its Map shape";
            }
            for (const auto& error : map_preview.errors)
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s", error.c_str());
            for (const auto& error : preview_render_state.errors)
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s", error.c_str());
            draw_transform_editor(session, selected_instances, transform_editor, status);
            ImGui::EndChild();
            ImGui::SameLine(0.0F, 2.0F);
            ImGui::InvisibleButton("##map-inspector-splitter", {6.0F, -1.0F});
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                if (ImGui::IsItemActive())
                    selection_pane_width = std::clamp(
                        selection_pane_width - ImGui::GetIO().MouseDelta.x,
                        300.0F,
                        std::max(300.0F,
                                 available_width - layers_pane_width - 336.0F));
            }
            ImGui::SameLine(0.0F, 2.0F);
            ImGui::BeginChild("map-selection-pane", {0.0F, 0.0F}, true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ui_map_inspector_x = ImGui::GetWindowPos().x;
            ImGui::SeparatorText("Inspector");
            const auto collisions_label = "Collisions (" +
                std::to_string(map.collisions.size()) + ")";
            if (ImGui::CollapsingHeader(
                    collisions_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Collisions: %zu", map.collisions.size());
            ImGui::SetNextItemWidth(150.0F);
            ImGui::Combo("New collision kind", &new_collision_kind,
                         "circle\0capsule\0polygon\0chain\0");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Collision layer", &new_collision_layer);
            ImGui::TextDisabled("Collision layers:");
            for (const auto& layer : map.layers) {
                if (layer.kind != fabric::project::MapLayerKind::collision)
                    continue;
                ImGui::SameLine();
                if (ImGui::SmallButton(layer.id.c_str()))
                    new_collision_layer = layer.id;
            }
            ImGui::Checkbox("New collision sensor", &new_collision_sensor);
            if (new_collision_kind == 3) {
                new_collision_sensor = false;
                ImGui::SameLine();
                ImGui::TextDisabled("chains are solid and cannot be triggers");
            }
            ImGui::SetNextItemWidth(220.0F);
            ImGui::DragFloat2("New collision center (world units)",
                              &new_collision_center.x, 0.1F);
            draw_technical_tooltip("Center of the new collision shape in world space.");
            if (new_collision_kind <= 1) {
                ImGui::SetNextItemWidth(180.0F);
                ImGui::DragFloat("New collision radius (world units)", &new_collision_radius,
                                 0.1F, 0.01F, 4096.0F);
                draw_technical_tooltip("Radius of the new circle or capsule shape.");
            }
            if (new_collision_kind == 1) {
                ImGui::SetNextItemWidth(180.0F);
                ImGui::DragFloat("New capsule length (world units)", &new_collision_length,
                                 0.1F, 0.0F, 4096.0F);
                draw_technical_tooltip("Straight section length of the new capsule.");
            }
            ImGui::BeginDisabled(new_collision_layer.empty() ||
                                 new_collision_radius <= 0.0F);
            if (ImGui::Button("Add collision")) {
                fabric::project::CollisionShape shape{
                    .kind = static_cast<fabric::project::CollisionShapeKind>(
                        new_collision_kind),
                    .layer_id = new_collision_layer,
                    .sensor = new_collision_sensor,
                    .center = new_collision_center,
                    .radius = new_collision_radius,
                    .length = new_collision_length};
                if (shape.kind ==
                    fabric::project::CollisionShapeKind::polygon)
                    shape.points = {
                        {new_collision_center.x - 1.0F,
                         new_collision_center.y - 1.0F},
                        {new_collision_center.x + 1.0F,
                         new_collision_center.y - 1.0F},
                        {new_collision_center.x,
                         new_collision_center.y + 1.0F}};
                else if (shape.kind ==
                         fabric::project::CollisionShapeKind::chain)
                    shape.points = {
                        {new_collision_center.x - 1.0F,
                         new_collision_center.y},
                        {new_collision_center.x + 1.0F,
                         new_collision_center.y}};
                const auto added = session.add_collision_shape(
                    std::move(shape));
                status = added ? "Collision added"
                               : "Collision creation rejected";
                if (added)
                    selected_collision_index = static_cast<int>(
                        session.map()->collisions.size() - 1U);
            }
            ImGui::EndDisabled();
            draw_disabled_reason(new_collision_layer.empty() ||
                                     new_collision_radius <= 0.0F,
                                 "Choose a collision layer and enter a positive radius.");
            for (std::size_t collision_index = 0; collision_index < map.collisions.size();
                 ++collision_index) {
                const auto& collision = map.collisions[collision_index];
                ImGui::PushID(static_cast<int>(collision_index));
                const auto label = "[" + std::to_string(collision_index) + "] " +
                                   collision_shape_text(collision) + " / layer " +
                                   collision.layer_id;
                if (ImGui::Selectable(label.c_str(),
                                      selected_collision_index ==
                                          static_cast<int>(collision_index))) {
                    selected_collision_index = static_cast<int>(collision_index);
                }
                ImGui::PopID();
            }
            if (selected_collision_index >= 0 &&
                static_cast<std::size_t>(selected_collision_index) < map.collisions.size()) {
                if (collision_editor_index != selected_collision_index) {
                    collision_editor_index = selected_collision_index;
                    collision_editor = map.collisions[static_cast<std::size_t>(
                        selected_collision_index)];
                }
                ImGui::SeparatorText("Selected collision");
                ImGui::Text("Layer: %s", collision_editor.layer_id.c_str());
                ImGui::Text("Kind: %s", collision_shape_text(collision_editor).c_str());
                ImGui::Checkbox("Sensor", &collision_editor.sensor);
                ImGui::SetNextItemWidth(220.0F);
                ImGui::DragFloat2("Center (world units)", &collision_editor.center.x, 0.1F);
                draw_technical_tooltip("Selected collision center in map world space.");
                if (collision_editor.kind == fabric::project::CollisionShapeKind::circle ||
                    collision_editor.kind == fabric::project::CollisionShapeKind::capsule) {
                    ImGui::SetNextItemWidth(220.0F);
                    ImGui::DragFloat("Radius (world units)", &collision_editor.radius, 0.1F, 0.0F,
                                     4096.0F);
                    draw_technical_tooltip("Selected circle or capsule radius.");
                }
                if (collision_editor.kind == fabric::project::CollisionShapeKind::capsule) {
                    ImGui::SetNextItemWidth(220.0F);
                    ImGui::DragFloat("Length (world units)", &collision_editor.length, 0.1F, 0.0F,
                                     4096.0F);
                    draw_technical_tooltip("Selected capsule straight section length.");
                }
                if (collision_editor.kind == fabric::project::CollisionShapeKind::polygon ||
                    collision_editor.kind == fabric::project::CollisionShapeKind::chain) {
                    ImGui::Text("Points: %zu", collision_editor.points.size());
                    for (std::size_t point_index = 0;
                         point_index < collision_editor.points.size(); ++point_index) {
                        ImGui::PushID(static_cast<int>(point_index));
                        ImGui::SetNextItemWidth(220.0F);
                        ImGui::DragFloat2("Point (world units)", &collision_editor.points[point_index].x,
                                          0.1F);
                        draw_technical_tooltip("Collision polygon or chain point in map world space.");
                        ImGui::PopID();
                    }
                    const auto minimum_points = collision_editor.kind ==
                        fabric::project::CollisionShapeKind::polygon ? 3U : 2U;
                    ImGui::BeginDisabled(collision_editor.points.size() <= minimum_points);
                    if (ImGui::Button("Remove last point")) collision_editor.points.pop_back();
                    ImGui::EndDisabled();
                    draw_disabled_reason(collision_editor.points.size() <= minimum_points,
                                         "The shape must keep its minimum number of points.");
                    ImGui::SameLine();
                    if (ImGui::Button("Add point")) collision_editor.points.push_back({});
                }
                if (ImGui::Button("Apply collision")) {
                    const auto applied = session.set_collision_shape(
                        static_cast<std::size_t>(selected_collision_index), collision_editor);
                    status = applied ? "Collision updated" :
                                       "Collision update rejected (layer locked or invalid)";
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete collision...")) {
                    remove_collision_request = selected_collision_index;
                    ImGui::OpenPopup("Delete collision?");
                }
            }
            if (ImGui::BeginPopupModal("Delete collision?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                const auto valid = remove_collision_request >= 0 &&
                    static_cast<std::size_t>(remove_collision_request) <
                        map.collisions.size();
                std::size_t trigger_references = 0;
                if (valid) {
                    trigger_references = static_cast<std::size_t>(std::ranges::count_if(
                        map.triggers, [&](const auto& trigger) {
                            return trigger.collision_index ==
                                static_cast<std::size_t>(remove_collision_request);
                        }));
                    ImGui::Text("Delete collision %d?", remove_collision_request);
                    ImGui::TextWrapped(
                        "The collision is removed from the map. Triggers referencing it are protected and must be removed first.");
                }
                ImGui::BeginDisabled(!valid || trigger_references != 0U);
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4{0.62F, 0.16F, 0.14F, 1.0F});
                if (ImGui::Button("Delete collision") &&
                    session.remove_collision_shape(
                        static_cast<std::size_t>(remove_collision_request))) {
                    selected_collision_index = -1;
                    collision_editor_index = -1;
                    remove_collision_request = -1;
                    status = "Collision deleted";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                draw_disabled_reason(!valid || trigger_references != 0U,
                                     "Select a collision without remaining trigger references before deleting it.");
                if (trigger_references != 0U)
                    ImGui::TextDisabled("Blocked: %zu trigger reference(s) remain.",
                                        trigger_references);
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    remove_collision_request = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            }
            const bool events_open = ImGui::CollapsingHeader(
                ("Events (" + std::to_string(map.events.size()) +
                 ")##map-events-tree").c_str());
            if (events_open) {
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
            draw_disabled_reason(event_id.empty(),
                                 "Enter an event id before declaring it.");
            for (const auto& event_definition : map.events) {
                const auto selected = selected_event_id == event_definition.id.value;
                if (ImGui::Selectable(event_definition.id.value.c_str(), selected))
                    selected_event_id = event_definition.id.value;
                ImGui::SameLine();
                ImGui::PushID((event_definition.id.value + "-delete").c_str());
                if (ImGui::SmallButton("Delete...")) {
                    remove_event_request = event_definition.id.value;
                    ImGui::OpenPopup("Delete event?");
                }
                ImGui::PopID();
            }
            if (!selected_event_id.empty()) {
                const auto event_definition = std::find_if(
                    map.events.begin(), map.events.end(), [&](const auto& event) {
                        return event.id.value == selected_event_id;
                    });
                if (event_definition != map.events.end()) {
                    if (event_editor_index != static_cast<int>(
                            std::distance(map.events.begin(), event_definition))) {
                        event_editor_index = static_cast<int>(
                            std::distance(map.events.begin(), event_definition));
                        event_payload_editor = event_definition->payload;
                    }
                    ImGui::SeparatorText("Selected event payload");
                    for (std::size_t property_index = 0;
                         property_index < event_payload_editor.size(); ++property_index) {
                        ImGui::PushID(static_cast<int>(property_index));
                        ImGui::BulletText("%s = %s", event_payload_editor[property_index].id.c_str(),
                                          property_value_text(
                                              event_payload_editor[property_index].value).c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove"))
                            event_payload_editor.erase(event_payload_editor.begin() +
                                                       static_cast<std::ptrdiff_t>(property_index));
                        ImGui::PopID();
                        if (property_index >= event_payload_editor.size()) break;
                    }
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::InputText("Payload property id", &event_property_id);
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::Combo("Payload type", &event_property_kind,
                                 "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                    ImGui::SetNextItemWidth(180.0F);
                    if (event_property_kind == 5)
                        static_cast<void>(draw_typed_resource_id_picker(
                            "Payload resource", resource_catalog.resources(),
                            event_resource_kind, event_property_value));
                    else
                        ImGui::InputText("Payload value", &event_property_value);
                    if (!event_property_value.empty())
                        draw_value_parse_error(
                            parse_override_value(event_property_kind,
                                                 event_property_value),
                            "Payload value", "Use true/false, a number, x,y, or a resource id.");
                    ImGui::BeginDisabled(event_property_id.empty() ||
                                         event_property_value.empty());
                    if (ImGui::Button("Apply payload property")) {
                        auto value = parse_override_value(event_property_kind,
                                                          event_property_value);
                        if (value && event_property_kind == 5) {
                            if (auto* reference = std::get_if<
                                    fabric::project::ResourceReference>(&*value))
                                reference->expected_type = std::string{
                                    resource_contract_for_kind(event_resource_kind)};
                        }
                        if (value) {
                            const auto existing = std::find_if(
                                event_payload_editor.begin(), event_payload_editor.end(),
                                [&](const auto& property) {
                                    return property.id == event_property_id;
                                });
                            if (existing != event_payload_editor.end()) existing->value = *value;
                            else event_payload_editor.push_back({event_property_id, *value});
                            const auto applied = session.set_event_payload(
                                {.value = selected_event_id}, event_payload_editor);
                            status = applied ? "Event payload updated" :
                                               "Event payload rejected";
                            if (applied) {
                                event_property_id.clear();
                                event_property_value.clear();
                            }
                        } else status = "Event payload value rejected";
                    }
                    ImGui::EndDisabled();
                    draw_disabled_reason(event_property_id.empty() || event_property_value.empty(),
                                         "Enter a payload property id and value before applying it.");
                }
            }
            if (ImGui::BeginPopupModal("Delete event?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                const auto event = std::ranges::find(
                    map.events, remove_event_request,
                    [](const auto& item) { return item.id.value; });
                const auto valid = event != map.events.end();
                const auto referenced = valid && std::ranges::any_of(
                    map.triggers, [&](const auto& trigger) {
                        return trigger.event_id.value == remove_event_request;
                    });
                if (valid) {
                    ImGui::Text("Delete event '%s'?", remove_event_request.c_str());
                    ImGui::TextDisabled("The event declaration and payload will be removed; map triggers must be removed first.");
                }
                ImGui::BeginDisabled(!valid || referenced);
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4{0.62F, 0.16F, 0.14F, 1.0F});
                if (ImGui::Button("Delete event") &&
                    session.remove_event({.value = remove_event_request})) {
                    if (selected_event_id == remove_event_request)
                        selected_event_id.clear();
                    remove_event_request.clear();
                    status = "Event deleted";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                draw_disabled_reason(!valid || referenced,
                                     "Select an event with no remaining trigger references before deleting it.");
                if (referenced)
                    ImGui::TextDisabled("Blocked: one or more triggers still reference this event.");
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    remove_event_request.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            }
            const auto triggers_label = "Triggers (" +
                std::to_string(map.triggers.size()) + ")";
            if (ImGui::CollapsingHeader(triggers_label.c_str())) {
            for (std::size_t trigger_index = 0; trigger_index < map.triggers.size();
                 ++trigger_index) {
                const auto& trigger = map.triggers[trigger_index];
                ImGui::PushID(static_cast<int>(trigger_index));
                const auto label = trigger.id + " -> " + trigger.event_id.value +
                                   " (collision " + std::to_string(trigger.collision_index) + ")";
                if (ImGui::Selectable(label.c_str(), selected_trigger_index ==
                                      static_cast<int>(trigger_index)))
                    selected_trigger_index = static_cast<int>(trigger_index);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    remove_trigger_request = trigger.id;
                    ImGui::OpenPopup("Delete trigger?");
                }
                ImGui::PopID();
            }
            if (ImGui::BeginPopupModal("Delete trigger?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                const auto trigger = std::ranges::find(
                    map.triggers, remove_trigger_request,
                    [](const auto& item) { return item.id; });
                const auto valid = trigger != map.triggers.end();
                if (valid) {
                    ImGui::Text("Delete trigger '%s'?", remove_trigger_request.c_str());
                    ImGui::TextDisabled("The collision-to-event link will be removed; the collision and event remain intact.");
                }
                ImGui::BeginDisabled(!valid);
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4{0.62F, 0.16F, 0.14F, 1.0F});
                if (ImGui::Button("Delete trigger") &&
                    session.remove_trigger({.value = remove_trigger_request})) {
                    remove_trigger_request.clear();
                    status = "Trigger deleted";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                draw_disabled_reason(!valid,
                                     "Select a trigger before deleting it.");
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    remove_trigger_request.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("New trigger id", &trigger_id);
            ImGui::SetNextItemWidth(180.0F);
            if (map.events.empty()) {
                ImGui::TextDisabled("Trigger event: declare an event first");
                trigger_event_id.clear();
            } else {
                std::vector<const char*> event_labels;
                event_labels.reserve(map.events.size());
                for (const auto& event : map.events)
                    event_labels.push_back(event.id.value.c_str());
                int selected_event = 0;
                for (std::size_t index = 0; index < map.events.size(); ++index)
                    if (map.events[index].id.value == trigger_event_id)
                        selected_event = static_cast<int>(index);
                if (ImGui::Combo("Trigger event", &selected_event,
                                event_labels.data(), static_cast<int>(event_labels.size())))
                    trigger_event_id = map.events[static_cast<std::size_t>(selected_event)].id.value;
            }
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputInt("Collision index", &trigger_collision_index);
            const auto valid_trigger_collision = [&](const int index) {
                if (index < 0 || static_cast<std::size_t>(index) >=
                                     map.collisions.size())
                    return false;
                const auto& collision =
                    map.collisions[static_cast<std::size_t>(index)];
                return collision.sensor && collision.kind !=
                    fabric::project::CollisionShapeKind::chain;
            };
            if (!valid_trigger_collision(trigger_collision_index))
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                   "Choose a closed sensor collision");
            ImGui::BeginDisabled(trigger_id.empty() || trigger_event_id.empty() ||
                                 !valid_trigger_collision(
                                     trigger_collision_index));
            if (ImGui::Button("Add trigger")) {
                const auto added = session.add_trigger(
                    {trigger_id, "triggers", static_cast<std::size_t>(trigger_collision_index),
                     {.value = trigger_event_id}, {}});
                status = added ? "Trigger added" : "Trigger rejected";
                if (added) {
                    trigger_id.clear();
                    trigger_event_id.clear();
                    trigger_collision_index = 0;
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(trigger_id.empty() || trigger_event_id.empty() ||
                                     !valid_trigger_collision(trigger_collision_index),
                                 "Enter a trigger id, choose an event and choose a closed sensor collision.");
            if (selected_trigger_index >= 0 &&
                static_cast<std::size_t>(selected_trigger_index) < map.triggers.size()) {
                if (trigger_editor_index != selected_trigger_index) {
                    trigger_editor_index = selected_trigger_index;
                    trigger_editor = map.triggers[static_cast<std::size_t>(selected_trigger_index)];
                    trigger_editor_collision_index =
                        static_cast<int>(trigger_editor.collision_index);
                }
                ImGui::SeparatorText("Selected trigger");
                ImGui::Text("Id: %s", trigger_editor.id.c_str());
                ImGui::SetNextItemWidth(220.0F);
                if (map.events.empty()) {
                    ImGui::TextDisabled("Event: no declared events");
                } else {
                    std::vector<const char*> event_labels;
                    event_labels.reserve(map.events.size());
                    for (const auto& event : map.events)
                        event_labels.push_back(event.id.value.c_str());
                    int selected_event = 0;
                    for (std::size_t index = 0; index < map.events.size(); ++index)
                        if (map.events[index].id == trigger_editor.event_id)
                            selected_event = static_cast<int>(index);
                    if (ImGui::Combo("Event", &selected_event, event_labels.data(),
                                    static_cast<int>(event_labels.size())))
                        trigger_editor.event_id = map.events[static_cast<std::size_t>(selected_event)].id;
                }
                ImGui::SetNextItemWidth(220.0F);
                ImGui::InputInt("Collision index", &trigger_editor_collision_index);
                const auto event_definition = std::find_if(
                    map.events.begin(), map.events.end(), [&](const auto& event) {
                        return event.id == trigger_editor.event_id;
                    });
                if (event_definition != map.events.end()) {
                    ImGui::Text("Event payload:");
                    for (const auto& property : event_definition->payload)
                        ImGui::BulletText("%s = %s", property.id.c_str(),
                                          property_value_text(property.value).c_str());
                }
                ImGui::Text("Trigger overrides:");
                for (std::size_t property_index = 0;
                     property_index < trigger_editor.properties.size();
                     ++property_index) {
                    const auto property = trigger_editor.properties[property_index];
                    ImGui::PushID(static_cast<int>(property_index));
                    ImGui::BulletText("%s = %s", property.id.c_str(),
                                      property_value_text(property.value).c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove override")) {
                        trigger_editor.properties.erase(
                            trigger_editor.properties.begin() +
                            static_cast<std::ptrdiff_t>(property_index));
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Trigger property id", &trigger_property_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Trigger property type", &trigger_property_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                if (trigger_property_kind == 5)
                    static_cast<void>(draw_typed_resource_id_picker(
                        "Trigger resource", resource_catalog.resources(),
                        trigger_resource_kind, trigger_property_value));
                else
                    ImGui::InputText("Trigger property value",
                                     &trigger_property_value);
                if (!trigger_property_value.empty())
                    draw_value_parse_error(
                        parse_override_value(trigger_property_kind,
                                             trigger_property_value),
                        "Trigger property value",
                        "Use true/false, a number, x,y, or a resource id.");
                ImGui::BeginDisabled(trigger_property_id.empty() ||
                                     trigger_property_value.empty());
                if (ImGui::Button("Set trigger override")) {
                    auto value = parse_override_value(
                        trigger_property_kind, trigger_property_value);
                    if (value && trigger_property_kind == 5) {
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(&*value))
                            reference->expected_type = std::string{
                                resource_contract_for_kind(trigger_resource_kind)};
                    }
                    if (value) {
                        const auto existing = std::ranges::find(
                            trigger_editor.properties, trigger_property_id,
                            &fabric::project::MapProperty::id);
                        if (existing == trigger_editor.properties.end())
                            trigger_editor.properties.push_back(
                                {trigger_property_id, *value});
                        else
                            existing->value = *value;
                        trigger_property_id.clear();
                        trigger_property_value.clear();
                    } else {
                        status = "Trigger property value rejected";
                    }
                }
                ImGui::EndDisabled();
                draw_disabled_reason(trigger_property_id.empty() || trigger_property_value.empty(),
                                     "Enter a trigger property id and value before setting the override.");
                if (!valid_trigger_collision(trigger_editor_collision_index))
                    ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                       "Trigger collision must be a closed sensor");
                ImGui::BeginDisabled(
                                     !valid_trigger_collision(
                                         trigger_editor_collision_index) ||
                                     trigger_editor.event_id.value.empty());
                if (ImGui::Button("Apply trigger")) {
                    trigger_editor.collision_index = static_cast<std::size_t>(
                        trigger_editor_collision_index);
                    const auto applied = session.set_trigger(
                        static_cast<std::size_t>(selected_trigger_index), trigger_editor);
                    status = applied ? "Trigger updated" :
                                       "Trigger update rejected (event, collision or layer)";
                }
                ImGui::EndDisabled();
                draw_disabled_reason(!valid_trigger_collision(trigger_editor_collision_index) ||
                                         trigger_editor.event_id.value.empty(),
                                     "Choose a valid sensor collision and event before applying the trigger.");
            }
            if (selected_instances.size() == 1U) {
                const fabric::core::ResourceId selected_id{selected_instances.front()};
                ImGui::SeparatorText("Selected instance properties");
                const auto instance = std::ranges::find(
                    map.instances, selected_id.value,
                    &fabric::project::MapInstance::id);
                std::optional<fabric::core::ResourceId> selected_entity;
                if (instance != map.instances.end()) {
                    if (instance->entity) selected_entity = instance->entity->id;
                    else if (instance->prefab) {
                        const auto prefab = std::ranges::find(
                            map.prefabs, instance->prefab->id.value,
                            &fabric::project::PrefabDefinition::id);
                        if (prefab != map.prefabs.end())
                            selected_entity = prefab->entity.id;
                    }
                }
                if (instance != map.instances.end()) {
                    ImGui::SeparatorText("Path follower");
                    if (focus_path_follower_panel) {
                        ImGui::SetScrollHereY(0.0F);
                        focus_path_follower_panel = false;
                    }
                    if (path_follower_bound_instance != instance->id) {
                        path_follower_bound_instance = instance->id;
                        if (instance->path_follower) {
                            path_follower_id = instance->path_follower->path.id.value;
                            path_follower_progress = instance->path_follower->progress;
                            path_follower_speed = instance->path_follower->speed;
                            path_follower_loop = instance->path_follower->loop;
                            path_follower_orient = instance->path_follower->orient_to_path;
                            path_follower_rotation_offset = instance->path_follower->rotation_offset_degrees;
                        } else {
                            path_follower_id.clear();
                            path_follower_progress = 0.0F;
                            path_follower_speed = 0.0F;
                            path_follower_loop = true;
                            path_follower_orient = true;
                            path_follower_rotation_offset = 0.0F;
                        }
                    }
                    if (session.manifest()) {
                        draw_resource_picker(
                            "Path asset", session.project_root() /
                                session.manifest()->directories.assets / "paths",
                            ".textured-path.json", path_follower_id, &resource_catalog);
                    }
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::DragFloat("Progress", &path_follower_progress, 0.01F, 0.0F, 1.0F);
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::DragFloat("Speed (world units/s)", &path_follower_speed, 0.1F, -4096.0F, 4096.0F);
                    ImGui::Checkbox("Loop", &path_follower_loop);
                    ImGui::Checkbox("Orient to tangent", &path_follower_orient);
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::DragFloat("Rotation offset (degrees)", &path_follower_rotation_offset, 1.0F);
                    const bool path_enabled = !path_follower_id.empty();
                    bool path_reference_valid = path_enabled;
                    if (path_enabled && session.manifest()) {
                        const auto path_document = fabric::project::textured_path_document_path(
                            *session.manifest(), {.value = path_follower_id});
                        const auto loaded_path = fabric::project::load_textured_path(
                            session.project_root(), *session.manifest(), path_document);
                        path_reference_valid = loaded_path.ok();
                        if (!path_reference_valid)
                            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                "Path is missing or invalid; choose another asset.");
                    }
                    ImGui::BeginDisabled(!path_reference_valid);
                    if (ImGui::Button("Apply path follower")) {
                        fabric::project::PathFollowerState follower{
                            .path = {{.value = path_follower_id}, "texturedPath"},
                            .progress = path_follower_progress,
                            .speed = path_follower_speed,
                            .loop = path_follower_loop,
                            .orient_to_path = path_follower_orient,
                            .rotation_offset_degrees = path_follower_rotation_offset};
                        status = session.set_instance_path_follower(selected_id, std::move(follower))
                            ? "Path follower configured" : "Path follower rejected";
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Remove path follower")) {
                        status = session.set_instance_path_follower(selected_id, std::nullopt)
                            ? "Path follower removed" : "Path follower removal rejected";
                    }
                    draw_disabled_reason(!path_reference_valid,
                        path_enabled ? "The selected path document is missing or invalid."
                                     : "Choose a textured path before applying the follower.");
                    ImGui::SeparatorText("Animation");
                    ImGui::TextWrapped(
                        "Create a clip that drives this instance from the current path progress.");
                    const bool can_create_path_animation = path_reference_valid &&
                        selected_entity.has_value();
                    ImGui::BeginDisabled(!can_create_path_animation);
                    if (ImGui::Button("Create path animation") ||
                        create_path_animation_request) {
                        create_path_animation_request = false;
                        fabric::project::PathFollowerState follower{
                            .path = {{.value = path_follower_id}, "texturedPath"},
                            .progress = path_follower_progress,
                            .speed = path_follower_speed,
                            .loop = path_follower_loop,
                            .orient_to_path = path_follower_orient,
                            .rotation_offset_degrees = path_follower_rotation_offset};
                        const auto follower_applied = session.set_instance_path_follower(
                            selected_id, follower);
                        if (!follower_applied) {
                            status = "Path follower could not be applied; animation not created";
                        } else {
                            fabric::editor::CreateAnimationPrompt prompt{
                            .name = instance->id + " path animation",
                            .preview_entity_id = selected_entity->value,
                            .generic_preview = false,
                            .duration = 1.0,
                            .loop = true};
                            std::size_t suffix = 1U;
                            while (std::ranges::any_of(resource_catalog.resources(),
                                [&](const auto& resource) {
                                    return resource.kind == fabric::editor::StudioResourceKind::animation &&
                                        resource.id == prompt.resource_id_for_document(
                                            resource_catalog.project_root(), *resource_catalog.manifest());
                                })) {
                                prompt.name = instance->id + " path animation " +
                                    std::to_string(++suffix);
                            }
                            const auto created = resource_catalog.create_animation(prompt);
                            const auto animation_id = prompt.resource_id_for_document(
                                resource_catalog.project_root(), *resource_catalog.manifest());
                            const auto track = created &&
                                resource_catalog.set_selected_animation_segment(
                                    {.node_id = "root", .component_id = "pathFollower",
                                     .property_id = "progress"},
                                    0.0F, 0.0F, 1.0F, 1.0F,
                                    fabric::project::AnimationInterpolation::linear);
                            const auto saved = track && resource_catalog.save();
                            const auto attached = saved && session.set_instance_property(
                                selected_id,
                                {.id = "animation",
                                 .value = fabric::project::ResourceReference{
                                     {.value = animation_id.value}, "animation"}});
                            status = attached
                                ? "Path animation created and attached"
                                : "Path animation could not be attached; inspect diagnostics";
                        }
                    }
                    ImGui::EndDisabled();
                    draw_disabled_reason(!can_create_path_animation,
                        path_reference_valid
                            ? "Select an entity-backed instance before creating an animation."
                            : "Choose a valid textured path before creating an animation.");
                }
                const auto transformations = load_transformations(
                    session.project_root(), *session.manifest());
                const auto selected_transformation = std::ranges::find_if(
                    transformations, [&](const auto& value) {
                        return value.document.id.value == transformation_preview_id;
                    });
                const char* transformation_label =
                    selected_transformation == transformations.end()
                    ? "Choose a compatible transformation..."
                    : selected_transformation->document.name.c_str();
                if (ImGui::BeginCombo("Transformation preview",
                                      transformation_label)) {
                    for (const auto& value : transformations) {
                        if (!selected_entity ||
                            value.source_entity.id != *selected_entity) continue;
                        if (ImGui::Selectable(value.document.name.c_str(),
                                value.document.id.value ==
                                    transformation_preview_id))
                            transformation_preview_id = value.document.id.value;
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s -> %s",
                            value.source_entity.id.value.c_str(),
                            value.destination_entity.id.value.c_str());
                    }
                    ImGui::EndCombo();
                }
                const bool can_preview = !session.dirty() && selected_entity &&
                    selected_transformation != transformations.end() &&
                    selected_transformation->source_entity.id == *selected_entity;
                ImGui::BeginDisabled(!can_preview);
                if (ImGui::Button("Run atomic transformation preview")) {
                    fabric::runtime::PreviewRuntime preview_runtime;
                    const bool loaded = preview_runtime.load({
                        .project_root = session.project_root(),
                        .map_id = map.document.id,
                        .mode = fabric::runtime::RuntimeMode::smoke_test,
                        .trace = {.session_id = trace_session_id,
                                  .resource_id = map.document.id.value},
                        .log_output = &std::clog});
                    const bool transformed = loaded &&
                        preview_runtime.transform_instance(
                            selected_id.value,
                            selected_transformation->document.id);
                    const auto result_entity = transformed
                        ? preview_runtime.instance_entity_id(selected_id.value)
                        : std::nullopt;
                    transformation_preview_result = transformed && result_entity
                        ? "Preview: " + selected_entity->value + " -> " +
                            result_entity->value + ", " +
                            std::to_string(preview_runtime.packet_order().size()) +
                            " draw packet(s). The map was not modified."
                        : "Transformation preview failed; the source instance was kept.";
                }
                ImGui::EndDisabled();
                draw_disabled_reason(!can_preview,
                                     "Save the map, select an entity instance and choose its matching transformation.");
                if (session.dirty())
                    ImGui::TextDisabled(
                        "Save the map before running the isolated runtime preview.");
                if (!transformation_preview_result.empty())
                    ImGui::TextWrapped("%s", transformation_preview_result.c_str());
                ImGui::SetNextItemWidth(220.0F);
                ImGui::InputTextWithHint("##instance-property-filter",
                                         "Search instance properties",
                                         &instance_property_filter);
                for (const auto& property : session.effective_instance_properties(selected_id)) {
                    if (!instance_property_filter.empty() &&
                        property.id.find(instance_property_filter) == std::string::npos)
                        continue;
                    ImGui::BulletText("%s = %s", property.id.c_str(),
                                      property_value_text(property.value).c_str());
                }
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Instance property id", &instance_property_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Instance type", &instance_property_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                if (instance_property_kind == 5)
                    static_cast<void>(draw_typed_resource_id_picker(
                        "Instance resource", resource_catalog.resources(),
                        instance_resource_kind, instance_property_value));
                else
                    ImGui::InputText("Instance value", &instance_property_value);
                if (!instance_property_value.empty())
                    draw_value_parse_error(
                        parse_override_value(instance_property_kind,
                                             instance_property_value),
                        "Instance value", "Use true/false, a number, x,y, or a resource id.");
                ImGui::BeginDisabled(instance_property_id.empty() || instance_property_value.empty());
                if (ImGui::Button("Apply instance property")) {
                    auto value = parse_override_value(instance_property_kind,
                                                      instance_property_value);
                    if (value && instance_property_kind == 5) {
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(&*value))
                            reference->expected_type = std::string{
                                resource_contract_for_kind(instance_resource_kind)};
                    }
                    auto property = value
                        ? std::optional<fabric::project::MapProperty>{
                              {instance_property_id, *value}}
                        : std::nullopt;
                    if (property && property->id == "animation") {
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(&property->value))
                            reference->expected_type = "animation";
                        else property.reset();
                    }
                    const auto applied = property && session.set_instance_property(
                        selected_id, std::move(*property));
                    status = applied ? "Instance property applied" : "Instance property rejected";
                    if (applied) {
                        instance_property_id.clear();
                        instance_property_value.clear();
                    }
                }
                ImGui::EndDisabled();
                draw_disabled_reason(instance_property_id.empty() || instance_property_value.empty(),
                                     "Enter an instance property id and value before applying it.");
            }
            }
            const auto prefabs_label = "Prefabs (" +
                std::to_string(map.prefabs.size()) + ")";
            if (ImGui::CollapsingHeader(prefabs_label.c_str())) {
            ImGui::SetNextItemWidth(160.0F);
            ImGui::InputText("New prefab id", &new_prefab_id);
            if (session.manifest()) {
                draw_resource_picker(
                    "Prefab entity:", session.project_root() /
                        session.manifest()->directories.entities,
                    ".entity.json", new_prefab_entity, &resource_catalog);
                draw_resource_picker(
                    "Prefab mechanic (optional):", session.project_root() /
                        session.manifest()->directories.assets / "mechanics",
                    ".mechanic.json", new_prefab_mechanic,
                    &resource_catalog);
            }
            ImGui::BeginDisabled(new_prefab_id.empty() ||
                                 new_prefab_entity.empty());
            if (ImGui::Button("Create prefab")) {
                fabric::project::PrefabDefinition prefab{
                    .id = new_prefab_id,
                    .entity = {{.value = new_prefab_entity}, "entity"}};
                if (!new_prefab_mechanic.empty())
                    prefab.mechanic = fabric::project::ResourceReference{
                        {.value = new_prefab_mechanic}, "mechanic"};
                const auto created = session.add_prefab(std::move(prefab));
                status = created ? "Prefab created"
                                 : "Prefab creation rejected";
                if (created) {
                    selected_prefab = new_prefab_id;
                    new_prefab_id.clear();
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(new_prefab_id.empty() || new_prefab_entity.empty(),
                                 "Enter a prefab id and choose an entity resource before creating it.");
            for (const auto& prefab : map.prefabs) {
                const auto selected = selected_prefab == prefab.id;
                if (ImGui::Selectable(prefab.id.c_str(), selected)) selected_prefab = prefab.id;
            }
            if (!selected_prefab.empty()) {
                ImGui::Text("Selected prefab: %s", selected_prefab.c_str());
                const auto prefab = std::ranges::find(
                    map.prefabs, selected_prefab,
                    &fabric::project::PrefabDefinition::id);
                if (prefab != map.prefabs.end() && prefab->mechanic) {
                    ImGui::Text("Mechanic: %s",
                                prefab->mechanic->id.value.c_str());
                    const auto preview_instance = std::ranges::find_if(
                        map.instances, [&](const auto& instance) {
                            return instance.prefab &&
                                   instance.prefab->id.value == selected_prefab;
                        });
                    if (ImGui::Button(preview_instance == map.instances.end()
                            ? "Preview prefab mechanic"
                            : "Preview mechanic instance")) {
                        const auto opened = preview_instance == map.instances.end()
                            ? mechanic_session.open_prefab(
                                  session.project_root(), map,
                                  {.value = selected_prefab})
                            : mechanic_session.open_prefab_instance(
                                  session.project_root(), map,
                                  {.value = preview_instance->id});
                        status = opened ? "Prefab mechanic preview opened"
                                        : "Prefab mechanic preview rejected";
                        if (opened)
                            mechanic_editor.open_id =
                                prefab->mechanic->id.value;
                        if (opened) {
                            static_cast<void>(editor_context.open_document(
                                prefab->mechanic->id,
                                fabric::editor::EditorWorkspace::logic));
                            active_workspace = ActiveWorkspace::mechanic;
                        }
                    }
                    const auto graph = session.prefab_mechanic_graph(
                        {.value = selected_prefab});
                    if (graph) {
                        ImGui::TextDisabled("Mechanic parameters:");
                        for (const auto& parameter : graph->parameters) {
                            const auto selected = mechanic_override_parameter ==
                                                  parameter.id;
                            const auto label = parameter.name + " [" +
                                std::string{fabric::project::to_string(
                                    parameter.type)} + "]##" + parameter.id;
                            if (ImGui::Selectable(label.c_str(), selected)) {
                                mechanic_override_parameter = parameter.id;
                                const auto stored = std::ranges::find(
                                    prefab->mechanic_overrides, parameter.id,
                                    &fabric::project::MechanicParameterOverride::parameter_id);
                                mechanic_override_value = property_value_text(
                                    stored == prefab->mechanic_overrides.end()
                                        ? parameter.default_value : stored->value);
                            }
                        }
                        const auto parameter = std::ranges::find(
                            graph->parameters, mechanic_override_parameter,
                            &fabric::project::MechanicParameterDefinition::id);
                        if (parameter != graph->parameters.end()) {
                            ImGui::SetNextItemWidth(180.0F);
                            ImGui::InputText("Mechanic value",
                                             &mechanic_override_value);
                            if (!mechanic_override_value.empty()) {
                                const auto parsed = parse_mechanic_override_value(
                                    *parameter, mechanic_override_value);
                                if (!parsed.has_value()) {
                                    ImGui::PushStyleColor(
                                        ImGuiCol_Text, {0.98F, 0.48F, 0.42F, 1.0F});
                                    ImGui::TextWrapped(
                                        "Mechanic value: value has the wrong format");
                                    ImGui::PopStyleColor();
                                    ImGui::TextDisabled(
                                        "Correction: match the selected mechanic parameter type.");
                                }
                            }
                            if (ImGui::Button("Apply mechanic override")) {
                                const auto value = parse_mechanic_override_value(
                                    *parameter, mechanic_override_value);
                                const auto applied = value &&
                                    session.set_prefab_mechanic_override(
                                        {.value = selected_prefab},
                                        {parameter->id, *value});
                                status = applied
                                    ? "Mechanic override applied"
                                    : "Mechanic override rejected by parameter type";
                                if (applied && mechanic_session.previewing_prefab(
                                                   {.value = selected_prefab})) {
                                    const auto refreshed_map = *session.map();
                                    const auto refreshed_instance = std::ranges::find_if(
                                        refreshed_map.instances,
                                        [&](const auto& instance) {
                                            return instance.prefab &&
                                                instance.prefab->id.value ==
                                                    selected_prefab;
                                        });
                                    if (refreshed_instance ==
                                        refreshed_map.instances.end())
                                        static_cast<void>(mechanic_session.open_prefab(
                                            session.project_root(), refreshed_map,
                                            {.value = selected_prefab}));
                                    else
                                        static_cast<void>(
                                            mechanic_session.open_prefab_instance(
                                                session.project_root(), refreshed_map,
                                                {.value = refreshed_instance->id}));
                                }
                            }
                        }
                    }
                }
                ImGui::SeparatorText("Visual/property overrides");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Property id", &override_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Type", &override_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                if (override_kind == 5)
                    static_cast<void>(draw_typed_resource_id_picker(
                        "Resource value", resource_catalog.resources(),
                        override_resource_kind, override_value));
                else
                    ImGui::InputText("Value", &override_value);
                if (!override_value.empty())
                    draw_value_parse_error(
                        parse_override_value(override_kind, override_value),
                        "Value", "Use true/false, a number, x,y, or a resource id.");
                ImGui::BeginDisabled(override_id.empty() || override_value.empty());
                if (ImGui::Button("Apply override")) {
                    auto value = parse_override_value(override_kind, override_value);
                    if (value && override_kind == 5) {
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(&*value))
                            reference->expected_type = std::string{
                                resource_contract_for_kind(override_resource_kind)};
                    }
                    auto property = value
                        ? std::optional<fabric::project::MapProperty>{
                              {override_id, *value}}
                        : std::nullopt;
                    if (property && property->id == "animation") {
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(&property->value))
                            reference->expected_type = "animation";
                        else property.reset();
                    }
                    const auto applied = property && session.set_prefab_override(
                        {.value = selected_prefab}, std::move(*property));
                    status = applied ? "Prefab override applied" : "Prefab override rejected";
                    if (applied) {
                        override_id.clear();
                        override_value.clear();
                    }
                }
                ImGui::EndDisabled();
                draw_disabled_reason(override_id.empty() || override_value.empty(),
                                     "Enter an override property id and value before applying it.");
            }
            }
            ImGui::EndChild();
            if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
            draw_validation_errors(package_errors, "Package");
            draw_validation_errors(session.errors());
        }
        } else if (active_workspace == ActiveWorkspace::scene) {
            draw_scene_workspace(
                scene_session, project_root, window, scene_editor, status,
                package_errors, resource_catalog, choose_folder);
        } else if (active_workspace == ActiveWorkspace::mechanic) {
            draw_mechanic_workspace(
                mechanic_session, session, mechanic_editor, status,
                resource_catalog, &mechanic_probe);
        } else {
            const bool publication_enabled = map_renderer.ready() &&
                preview_render_state.errors.empty();
            const std::string publication_disabled_reason = !map_renderer.ready()
                ? "Preview renderer unavailable: " +
                      map_renderer.initialization_error()
                : "Resolve render diagnostics before publication.";
            draw_publish_workspace(
                session, scene_session, window, publish_editor, status,
                package_errors, publication_enabled,
                publication_disabled_reason, prepare_package, choose_folder,
                &publish_probe);
        }
        ImGui::End();
        const auto* selected_document = editor_context.active_document();
        if (selected_document != nullptr &&
            selected_document->workspace ==
                fabric::editor::EditorWorkspace::map) {
            static_cast<void>(editor_context.set_view({
                .zoom = canvas_zoom,
                .pan = {canvas_pan.x, canvas_pan.y},
                .playhead = preview_time,
                .active_tool = placement_mode ? "place" : "select",
                .active_panel = "inspector",
            }));
            std::vector<fabric::core::ResourceId> stable_selections;
            stable_selections.reserve(selected_instances.size());
            for (const auto& id : selected_instances) {
                if (fabric::core::ResourceId::is_valid(id))
                    stable_selections.push_back({.value = id});
            }
            const auto stable_selection = stable_selections.empty()
                ? std::optional<fabric::core::ResourceId>{}
                : std::optional<fabric::core::ResourceId>{
                      stable_selections.front()};
            static_cast<void>(editor_context.set_selection_set(
                stable_selection, std::move(stable_selections)));
        }

        if (const auto ready = transition_guard.take_ready();
            ready == fabric::editor::SessionAction::quit) {
            running = false;
        }
        if (transition_guard.confirmation_required())
            ImGui::OpenPopup("Unsaved Map Studio documents");
        if (ImGui::BeginPopupModal("Unsaved Map Studio documents", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save changes before closing Map Studio?");
            if (session.dirty())
                ImGui::BulletText("Map: %s",
                                  session.map()
                                      ? session.map()->document.name.c_str()
                                      : "current map");
            if (mechanic_session.dirty())
                ImGui::BulletText("Mechanic: %s",
                                  mechanic_session.graph()
                                      ? mechanic_session.graph()->document.name.c_str()
                                      : "current mechanic");
            if (scene_session.dirty())
                ImGui::BulletText("Scene: %s",
                                  scene_session.scene()
                                      ? scene_session.scene()->document.name.c_str()
                                      : "current scene");
            if (e2e_mode && !e2e_modal_handled) {
                if (!transition_guard.confirmation_required() ||
                    !session.dirty() ||
                    read_binary_file(e2e_primary_path) != e2e_primary_contents ||
                    read_binary_file(e2e_autosave_path) != e2e_autosave_contents) {
                    fail_e2e("close request changed the active document or autosave");
                } else if (*e2e_mode == CloseE2eMode::save) {
                    const bool saved = session.save();
                    if (!saved || session.dirty() ||
                        transition_guard.resolve(
                            fabric::editor::UnsavedDecision::save, true) !=
                            fabric::editor::SessionAction::quit)
                        fail_e2e("Save did not finish the close request");
                    running = false;
                } else if (*e2e_mode != CloseE2eMode::save_failure) {
                    static_cast<void>(transition_guard.resolve(
                        fabric::editor::UnsavedDecision::cancel));
                    if (transition_guard.pending() || !session.dirty())
                        fail_e2e("Cancel did not preserve the dirty session");
                    transition_guard.request(
                        fabric::editor::SessionAction::quit, session.dirty());
                    if (transition_guard.resolve(
                            fabric::editor::UnsavedDecision::discard) !=
                        fabric::editor::SessionAction::quit)
                        fail_e2e("Discard did not finish the close request");
                    running = false;
                } else {
                    const auto backup = std::filesystem::path{
                        e2e_primary_path.string() + ".e2e-backup"};
                    std::error_code filesystem_error;
                    std::filesystem::rename(
                        e2e_primary_path, backup, filesystem_error);
                    const bool primary_backed_up = !filesystem_error;
                    if (primary_backed_up)
                        std::filesystem::create_directory(
                            e2e_primary_path, filesystem_error);
                    if (filesystem_error) {
                        fail_e2e("save failure could not be prepared");
                    } else {
                        const bool saved = session.save();
                        static_cast<void>(transition_guard.resolve(
                            fabric::editor::UnsavedDecision::save, saved));
                        if (saved || !transition_guard.confirmation_required() ||
                            !transition_guard.pending() || !session.dirty() ||
                            read_binary_file(backup) != e2e_primary_contents ||
                            read_binary_file(e2e_autosave_path) !=
                                e2e_autosave_contents)
                            fail_e2e("Save failure did not preserve recovery state");
                    }
                    if (primary_backed_up) {
                        filesystem_error.clear();
                        std::filesystem::remove(
                            e2e_primary_path, filesystem_error);
                        if (!filesystem_error)
                            std::filesystem::rename(
                                backup, e2e_primary_path, filesystem_error);
                    }
                    if (!primary_backed_up || filesystem_error ||
                        read_binary_file(e2e_primary_path) !=
                            e2e_primary_contents)
                        fail_e2e("primary document could not be restored");
                    if (!e2e_failed &&
                        transition_guard.resolve(
                            fabric::editor::UnsavedDecision::discard) !=
                            fabric::editor::SessionAction::quit)
                        fail_e2e("failed Save did not keep the close decision open");
                    running = false;
                }
                e2e_modal_handled = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Retry save and continue", {170.0F, 0.0F})) {
                if (save_dirty_documents()) {
                    ImGui::CloseCurrentPopup();
                    if (transition_guard.resolve(
                        fabric::editor::UnsavedDecision::save) ==
                        fabric::editor::SessionAction::quit) {
                        running = false;
                    }
                } else {
                    static_cast<void>(transition_guard.resolve(
                        fabric::editor::UnsavedDecision::save, false));
                    status = "Save failed; retry or discard the changes";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", {100.0F, 0.0F})) {
                ImGui::CloseCurrentPopup();
                if (transition_guard.resolve(
                        fabric::editor::UnsavedDecision::discard) ==
                    fabric::editor::SessionAction::quit) {
                    running = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100.0F, 0.0F})) {
                static_cast<void>(transition_guard.resolve(
                    fabric::editor::UnsavedDecision::cancel));
                ImGui::CloseCurrentPopup();
                status = "Close cancelled; unsaved changes kept";
            }
            ImGui::EndPopup();
        }
        ImGui::Render();
        glViewport(0, 0, static_cast<GLsizei>(ImGui::GetIO().DisplaySize.x),
                   static_cast<GLsizei>(ImGui::GetIO().DisplaySize.y));
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (mechanic_e2e && mechanic_probe.instance_action_clicked &&
            !mechanic_connection_removed) {
            mechanic_connection_removed = mechanic_session.disconnect(0U);
            mechanic_e2e_complete = mechanic_e2e_complete &&
                mechanic_connection_removed;
            if (!mechanic_connection_removed)
                fail_e2e("contextual mechanic could not prepare connection gesture");
        }
        if (mechanic_e2e && mechanic_probe.instance_action_seen &&
            mechanic_e2e_frame == 1U) {
            write_frame_capture(project_root, window,
                                "map-studio-mechanic-entry-e2e.ppm");
        }
        if (mechanic_e2e && mechanic_probe.source_seen &&
            mechanic_e2e_frame == 4U) {
            write_frame_capture(project_root, window,
                                "map-studio-mechanic-source-e2e.ppm");
        }
        if (mechanic_e2e && mechanic_probe.link_seen &&
            mechanic_probe.rotation_handle_moved &&
            mechanic_probe.joint_handle_moved &&
            mechanic_e2e_frame >= 26U &&
            !mechanic_e2e_capture_written) {
            write_frame_capture(project_root, window,
                                "map-studio-mechanic-graph-e2e.ppm");
            mechanic_e2e_capture_written = true;
        }
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::map &&
            placement_probe.mechanic.overlay_seen &&
            placement_probe.mechanic.parameter_handle_moved &&
            !mechanic_map_parameter_verified) {
            const auto body = std::ranges::find(
                mechanic_session.simulation().body_states(),
                std::string{"platform"},
                &fabric::physics::MechanicBodyState::node_id);
            const bool preview_changed =
                body != mechanic_session.simulation().body_states().end() &&
                body->size.x >
                    placement_probe.mechanic.parameter_original_size.x &&
                body->size.y >
                    placement_probe.mechanic.parameter_original_size.y;
            const bool saved = session.save();
            fabric::editor::MapSession reloaded_map;
            const bool reopened = saved && session.map() && reloaded_map.open(
                project_root, session.map()->document.id);
            bool override_reloaded = false;
            if (reopened && reloaded_map.map()) {
                const auto prefab = std::ranges::find(
                    reloaded_map.map()->prefabs,
                    std::string{"rotating-platform-prefab"},
                    &fabric::project::PrefabDefinition::id);
                if (prefab != reloaded_map.map()->prefabs.end()) {
                    const auto size_override = std::ranges::find(
                        prefab->mechanic_overrides, std::string{"size"},
                        &fabric::project::MechanicParameterOverride::parameter_id);
                    if (size_override != prefab->mechanic_overrides.end()) {
                        if (const auto* value = std::get_if<fabric::core::Vec2>(
                                &size_override->value)) {
                            override_reloaded = body !=
                                    mechanic_session.simulation().body_states().end() &&
                                *value == body->size;
                        }
                    }
                }
            }
            mechanic_map_parameter_verified = preview_changed && override_reloaded;
            if (!mechanic_map_parameter_verified) {
                fail_e2e(
                    "Map mechanic handle did not persist and rebuild its prefab override");
                running = false;
            }
        }
        if (mechanic_e2e && mechanic_map_parameter_verified &&
            active_workspace == ActiveWorkspace::map &&
            mechanic_map_e2e_frame >= 9U &&
            !mechanic_map_overlay_capture_written) {
            write_frame_capture(project_root, window,
                                "map-studio-mechanic-map-overlay-e2e.ppm");
            mechanic_map_overlay_capture_written = true;
        }
        if (mechanic_e2e && mechanic_map_overlay_capture_written &&
            active_workspace == ActiveWorkspace::mechanic &&
            !mechanic_map_reopen_verified) {
            const auto* mechanic_document = editor_context.active_document();
            const bool opened_exact_node = mechanic_document != nullptr &&
                mechanic_document->workspace ==
                    fabric::editor::EditorWorkspace::logic &&
                mechanic_document->id.value == "rotating-platform" &&
                mechanic_editor.selected_node == "platform";
            const bool returned = editor_context.go_back();
            const auto* map_document = editor_context.active_document();
            const bool restored_instance = returned && map_document != nullptr &&
                map_document->workspace == fabric::editor::EditorWorkspace::map &&
                map_document->selection_id == fabric::core::ResourceId{
                    .value = "rotating-platform-instance"};
            mechanic_map_reopen_verified = opened_exact_node && restored_instance;
            if (!mechanic_map_reopen_verified) {
                fail_e2e("Map shape did not open its exact mechanic node and history");
                running = false;
            } else {
                publish_e2e_frame = 0U;
                active_workspace = ActiveWorkspace::publish;
            }
        }
        if (mechanic_e2e && publish_probe.runtime_verified &&
            !publish_e2e_capture_written) {
            write_frame_capture(project_root, window,
                                "map-studio-publish-runtime-e2e.ppm");
            publish_e2e_capture_written = true;
        }
        if (mechanic_e2e && !mechanic_authoring_verified &&
            ++mechanic_e2e_frame == 28U) {
            const bool saved = mechanic_session.save();
            fabric::editor::MechanicSession reloaded;
            const bool reopened = saved && session.map() && reloaded.open(
                project_root, *session.map(), {.value = "rotating-platform"});
            bool spatial_value_reloaded = false;
            bool size_value_reloaded = false;
            bool rotation_value_reloaded = false;
            bool joint_value_reloaded = false;
            if (reopened) {
                const auto node = std::ranges::find(
                    reloaded.graph()->nodes, mechanic_probe.spatial_handle_node,
                    &fabric::project::MechanicNodeDefinition::id);
                if (node != reloaded.graph()->nodes.end()) {
                    const auto property = std::ranges::find(
                        node->properties, mechanic_probe.spatial_handle_property,
                        &fabric::project::MechanicNodeProperty::id);
                    if (property != node->properties.end()) {
                        if (const auto* value = std::get_if<fabric::core::Vec2>(
                                &property->value)) {
                            spatial_value_reloaded =
                                value->x > mechanic_probe.spatial_handle_original.x +
                                    0.9F &&
                                value->x < mechanic_probe.spatial_handle_original.x +
                                    1.1F &&
                                value->y == mechanic_probe.spatial_handle_original.y;
                        }
                    }
                }
                const auto body = std::ranges::find(
                    reloaded.graph()->nodes, std::string{"platform"},
                    &fabric::project::MechanicNodeDefinition::id);
                if (body != reloaded.graph()->nodes.end()) {
                    const auto size = std::ranges::find(
                        body->properties, std::string{"size"},
                        &fabric::project::MechanicNodeProperty::id);
                    if (size != body->properties.end()) {
                        if (const auto* value = std::get_if<fabric::core::Vec2>(
                                &size->value)) {
                            size_value_reloaded =
                                value->x > mechanic_probe.resize_handle_original.x +
                                    1.9F &&
                                value->x < mechanic_probe.resize_handle_original.x +
                                    2.1F &&
                                value->y > mechanic_probe.resize_handle_original.y +
                                    0.9F &&
                                value->y < mechanic_probe.resize_handle_original.y +
                                    1.1F;
                        }
                    }
                    const auto rotation = std::ranges::find(
                        body->properties, std::string{"rotation"},
                        &fabric::project::MechanicNodeProperty::id);
                    if (rotation != body->properties.end()) {
                        if (const auto* value = std::get_if<float>(
                                &rotation->value)) {
                            rotation_value_reloaded = std::abs(
                                *value - mechanic_probe.rotation_handle_original) > 5.0F;
                        }
                    }
                }
                const auto joint_target = std::ranges::find(
                    reloaded.graph()->nodes, mechanic_probe.joint_mutation_node,
                    &fabric::project::MechanicNodeDefinition::id);
                if (joint_target != reloaded.graph()->nodes.end()) {
                    const auto position = std::ranges::find(
                        joint_target->properties, std::string{"position"},
                        &fabric::project::MechanicNodeProperty::id);
                    if (position != joint_target->properties.end()) {
                        if (const auto* value = std::get_if<fabric::core::Vec2>(
                                &position->value)) {
                            joint_value_reloaded =
                                value->x > mechanic_probe.joint_handle_original.x +
                                    0.9F &&
                                value->x < mechanic_probe.joint_handle_original.x +
                                    1.1F &&
                                value->y == mechanic_probe.joint_handle_original.y;
                        }
                    }
                }
            }
            const bool navigated_back = editor_context.go_back();
            const auto* restored_map = editor_context.active_document();
            const bool map_selection_restored = navigated_back &&
                restored_map != nullptr &&
                restored_map->workspace ==
                    fabric::editor::EditorWorkspace::map &&
                restored_map->selection_id == fabric::core::ResourceId{
                    .value = "rotating-platform-instance"};
            const bool navigated_forward = editor_context.go_forward();
            const auto* restored_mechanic = editor_context.active_document();
            const bool mechanic_context_restored = navigated_forward &&
                restored_mechanic != nullptr &&
                restored_mechanic->workspace ==
                    fabric::editor::EditorWorkspace::logic &&
                restored_mechanic->id.value == "rotating-platform";
            mechanic_e2e_complete = mechanic_e2e_complete && reopened &&
                mechanic_connection_removed &&
                mechanic_probe.instance_action_seen &&
                mechanic_probe.instance_action_clicked &&
                map_selection_restored && mechanic_context_restored &&
                mechanic_probe.canvas_seen && mechanic_probe.link_seen &&
                mechanic_probe.spatial_canvas_seen &&
                mechanic_probe.spatial_handle_seen &&
                mechanic_probe.spatial_handle_moved && spatial_value_reloaded &&
                mechanic_probe.body_handle_seen &&
                mechanic_probe.resize_handle_seen &&
                mechanic_probe.resize_handle_moved && size_value_reloaded &&
                mechanic_probe.rotation_handle_seen &&
                mechanic_probe.rotation_handle_moved && rotation_value_reloaded &&
                mechanic_probe.joint_handle_seen &&
                mechanic_probe.joint_handle_moved && joint_value_reloaded &&
                mechanic_e2e_capture_written && reloaded.simulation().valid() &&
                std::ranges::find(reloaded.graph()->connections,
                                  mechanic_probe.expected_connection) !=
                    reloaded.graph()->connections.end() &&
                reloaded.step_once();
            if (!mechanic_e2e_complete)
                fail_e2e("mechanic canvas connection did not reload and simulate: " +
                    std::to_string(mechanic_probe.instance_action_seen) + "," +
                    std::to_string(mechanic_probe.instance_action_clicked) + "," +
                    std::to_string(mechanic_probe.instance_action_screen.x) + "," +
                    std::to_string(mechanic_probe.instance_action_screen.y) + "," +
                    std::to_string(mechanic_probe.source_screen.x) + "," +
                    std::to_string(mechanic_probe.source_screen.y) + "," +
                    std::to_string(map_selection_restored) + "," +
                    std::to_string(mechanic_context_restored) + "," +
                    std::to_string(mechanic_probe.canvas_seen) + "," +
                    std::to_string(mechanic_probe.source_clicked) + "," +
                    std::to_string(mechanic_probe.target_clicked) + "," +
                    std::to_string(mechanic_probe.link_seen) + "," +
                    std::to_string(mechanic_probe.spatial_canvas_seen) + "," +
                    std::to_string(mechanic_probe.spatial_handle_seen) + "," +
                    std::to_string(mechanic_probe.spatial_handle_moved) + "," +
                    std::to_string(spatial_value_reloaded) + "," +
                    std::to_string(mechanic_probe.body_handle_seen) + "," +
                    std::to_string(mechanic_probe.resize_handle_seen) + "," +
                    std::to_string(mechanic_probe.resize_handle_moved) + "," +
                    std::to_string(size_value_reloaded) + "," +
                    std::to_string(mechanic_probe.rotation_handle_seen) + "," +
                    std::to_string(mechanic_probe.rotation_handle_moved) + "," +
                    std::to_string(rotation_value_reloaded) + "," +
                    std::to_string(mechanic_probe.joint_handle_seen) + "," +
                    std::to_string(mechanic_probe.joint_handle_moved) + "," +
                    std::to_string(joint_value_reloaded) + ",selected=" +
                    mechanic_editor.selected_node);
            if (mechanic_e2e_complete) {
                mechanic_authoring_verified = true;
                mechanic_e2e_complete = editor_context.go_back();
                active_workspace = ActiveWorkspace::map;
            } else {
                running = false;
            }
        }
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::map)
            ++mechanic_map_e2e_frame;
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::map &&
            mechanic_map_e2e_frame == 20U &&
            !mechanic_map_parameter_verified) {
            fail_e2e("Map mechanic gesture timed out: frame=" +
                std::to_string(mechanic_map_e2e_frame) + ",frame-selection=" +
                std::to_string(placement_probe.frame_selection_seen) +
                ",body=" + std::to_string(
                    placement_probe.mechanic.parameter_body_seen) +
                ",handle=" + std::to_string(
                    placement_probe.mechanic.parameter_handle_seen) +
                ",moved=" + std::to_string(
                    placement_probe.mechanic.parameter_handle_moved));
            running = false;
        }
        if (mechanic_e2e && mechanic_authoring_verified &&
            active_workspace == ActiveWorkspace::publish &&
            ++publish_e2e_frame == 9U) {
            const auto destination = publish_editor.destination_parent /
                (session.map()->document.id.value + ".map-package");
            mechanic_e2e_complete = mechanic_e2e_complete &&
                publish_probe.validate_seen && publish_probe.validate_clicked &&
                publish_probe.publish_seen && publish_probe.publish_clicked &&
                publish_probe.dependency_seen && publish_probe.runtime_verified &&
                mechanic_map_parameter_verified &&
                mechanic_map_reopen_verified &&
                mechanic_map_overlay_capture_written &&
                publish_e2e_capture_written &&
                std::filesystem::is_regular_file(
                    destination /
                    fabric::project::map_package_manifest_filename);
            if (!mechanic_e2e_complete)
                fail_e2e("Publish workspace did not validate, publish and run: " +
                    std::to_string(publish_probe.validate_clicked) + "," +
                    std::to_string(publish_probe.publish_clicked) + "," +
                    std::to_string(publish_probe.dependency_seen) + "," +
                    std::to_string(publish_probe.runtime_verified) + "," +
                    std::to_string(publish_e2e_capture_written));
            running = false;
        }
        if (scene_e2e || transformation_e2e) running = false;
        if (placement_e2e && placement_probe.successful_placements == 2U &&
            placement_mode && keep_placement_active &&
            placement_resource_id == "textile-head-entity") {
            placement_context_observed = true;
        }
        if (placement_e2e && placement_context_observed &&
            placement_e2e_frame >= 14U && !placement_mode) {
            const bool context_preserved = keep_placement_active &&
                placement_resource_id == "textile-head-entity";
            const bool saved = context_preserved && session.save();
            fabric::editor::MapSession reloaded;
            const bool reopened = saved && reloaded.open(project_root, map_id);
            std::size_t authored_count = 0U;
            if (reopened) {
                authored_count = static_cast<std::size_t>(std::ranges::count_if(
                    reloaded.map()->instances, [](const auto& instance) {
                        return instance.entity &&
                            instance.entity->id.value == "textile-head-entity" &&
                            instance.id.starts_with("textile-head-entity-instance");
                    }));
            }
            if (!context_preserved || !reopened || authored_count != 2U)
                fail_e2e("continuous placement did not preserve context or reload two unique instances");
            else
                write_frame_capture(project_root, window,
                                    "map-studio-continuous-placement-e2e.ppm");
            running = false;
        }
        if (placement_e2e && ++placement_e2e_frame >= 30U && running) {
            fail_e2e(
                "continuous placement UI gestures timed out: canvas=" +
                std::to_string(placement_probe.canvas_seen) +
                ", hovered=" + std::to_string(placement_probe.canvas_hovered) +
                ", button=" +
                std::to_string(placement_probe.placement_button_seen) +
                ", mode=" + std::to_string(placement_mode) +
                ", placements=" +
                std::to_string(placement_probe.successful_placements) +
                ", target=" + std::to_string(placement_probe.canvas_center.x) +
                "," + std::to_string(placement_probe.canvas_center.y) +
                ", mouse=" + std::to_string(ImGui::GetIO().MousePos.x) +
                "," + std::to_string(ImGui::GetIO().MousePos.y));
            running = false;
        }
        if (ui_accessibility_test) {
            write_ui_accessibility_probe(
                project_root, window,
                (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0,
                command_palette_rendered);
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }

    const bool e2e_incomplete = e2e_failed ||
        (scene_e2e && !scene_e2e_complete) ||
        (transformation_e2e && !transformation_e2e_complete) ||
        (mechanic_e2e && !mechanic_e2e_complete);
    if (e2e_incomplete)
        write_e2e_failure_artifacts(project_root, window, status, session,
                                    package_errors);
    if (!layout_preferences_path.empty()) {
        std::string preference_error;
        if (!fabric::editor::save_layout_preferences(
                layout_preferences_path, layout, &preference_error)) {
            std::clog << preference_error << '\n';
        }
    }
    for (const auto& [_, texture] : map_textures) {
        if (texture.handle != 0U) glDeleteTextures(1, &texture.handle);
    }
    map_renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    NFD_Quit();
    SDL_Quit();
    return e2e_incomplete ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool e2e = argc == 5 && std::string_view{argv[1]} == "--e2e-close";
    const bool scene_e2e = argc == 4 &&
        std::string_view{argv[1]} == "--e2e-scene";
    const bool transformation_e2e = argc == 4 &&
        std::string_view{argv[1]} == "--e2e-transformation";
    const bool mechanic_e2e = argc == 4 &&
        std::string_view{argv[1]} == "--e2e-mechanic";
    const bool placement_e2e = argc == 4 &&
        std::string_view{argv[1]} == "--e2e-placement";
    const bool ui_accessibility_test = argc == 3 &&
        std::string_view{argv[1]} == "--ui-accessibility-test";
    const auto e2e_mode = e2e ? close_e2e_mode(argv[2]) : std::nullopt;
    if ((argc != 1 && argc != 3 && !e2e && !scene_e2e &&
         !transformation_e2e && !mechanic_e2e && !placement_e2e &&
         !ui_accessibility_test) ||
        (e2e && !e2e_mode)) {
        std::cerr << "usage: map_studio [project-directory map-id]\n"
                     "       map_studio --e2e-close "
                     "<clean|window|shortcut|save|save-failure> project-directory map-id\n"
                     "       map_studio --e2e-scene project-directory map-id\n"
                     "       map_studio --e2e-transformation "
                     "project-directory map-id\n"
                     "       map_studio --e2e-mechanic project-directory map-id\n"
                     "       map_studio --e2e-placement project-directory map-id\n"
                     "       map_studio --ui-accessibility-test project-directory\n";
        return 64;
    }
    const std::filesystem::path project = e2e ? argv[3]
        : scene_e2e ? argv[2]
        : transformation_e2e ? argv[2]
        : mechanic_e2e || placement_e2e ? argv[2]
        : ui_accessibility_test ? argv[2]
        : argc == 3 ? argv[1] : std::filesystem::path{};
    const fabric::core::ResourceId map_id{
        e2e ? argv[4]
        : scene_e2e || transformation_e2e || mechanic_e2e || placement_e2e ? argv[3]
        : ui_accessibility_test ? "textile-head-preview"
        : argc == 3 ? argv[2] : ""};
    return run(project, map_id, e2e_mode, scene_e2e, transformation_e2e,
               ui_accessibility_test, mechanic_e2e, placement_e2e);
}
