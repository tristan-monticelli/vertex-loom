#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/editor/session_transition.hpp"
#include "fabric/render/raster_image.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_stdlib.h>
#include <nfd.h>
#include <nfd_sdl2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr ImGuiWindowFlags fixed_panel_flags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

struct CreationUiState {
    fabric::editor::CreateProjectPrompt project;
    fabric::editor::CreateVectorArtworkPrompt artwork;
    std::optional<fabric::editor::CreateVectorArtworkPrompt> prepared_artwork;
    bool request_project{};
    bool request_artwork{};
    bool project_publish_attempted{};
};

enum class PreviewKind {
    none,
    texture,
    vector,
};

#if defined(__APPLE__)
constexpr const char* new_shortcut = "Cmd+N";
constexpr const char* open_shortcut = "Cmd+O";
constexpr const char* save_shortcut = "Cmd+S";
constexpr const char* import_shortcut = "Cmd+I";
constexpr const char* import_svg_shortcut = "Cmd+Shift+I";
constexpr const char* quit_shortcut = "Cmd+Q";
constexpr const char* undo_shortcut = "Cmd+Z";
constexpr const char* redo_shortcut = "Cmd+Shift+Z";
#else
constexpr const char* new_shortcut = "Ctrl+N";
constexpr const char* open_shortcut = "Ctrl+O";
constexpr const char* save_shortcut = "Ctrl+S";
constexpr const char* import_shortcut = "Ctrl+I";
constexpr const char* import_svg_shortcut = "Ctrl+Shift+I";
constexpr const char* quit_shortcut = "Ctrl+Q";
constexpr const char* undo_shortcut = "Ctrl+Z";
constexpr const char* redo_shortcut = "Ctrl+Shift+Z";
#endif

struct SourceImportFields {
    fabric::editor::ImportSourcePrompt prompt;
    bool attempted{false};
};

struct AssetPreview {
    GLuint texture{};
    std::uint32_t width{};
    std::uint32_t height{};
    PreviewKind kind{PreviewKind::none};
};

struct ImportUiState {
    SourceImportFields png;
    SourceImportFields svg;
};

struct ProjectSettingsUiState {
    std::string name;
    double pixels_per_unit{fabric::project::default_pixels_per_unit};
    bool request{};
};

struct CanvasUiState {
    float zoom{1.0F};
    ImVec2 pan{};
    std::size_t selected_node{};
};

void upload_preview(AssetPreview& preview,
                    const fabric::render::RasterImage& image) {
    if (preview.texture != 0U) {
        glDeleteTextures(1, &preview.texture);
    }
    glGenTextures(1, &preview.texture);
    glBindTexture(GL_TEXTURE_2D, preview.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 static_cast<GLsizei>(image.width),
                 static_cast<GLsizei>(image.height), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.rgba8.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    preview.width = image.width;
    preview.height = image.height;
}

bool import_texture(fabric::editor::ProjectSession& session,
                    SourceImportFields& fields, AssetPreview& preview) {
    fields.attempted = true;
    const auto id = fields.prompt.resource_id(session.project_root(),
                                              *session.manifest());
    if (!session.import_png(fields.prompt.source,
                            id,
                            fields.prompt.name)) {
        return false;
    }
    upload_preview(preview, session.imported_texture()->image);
    preview.kind = PreviewKind::texture;
    return true;
}

bool import_vector(fabric::editor::ProjectSession& session,
                   SourceImportFields& fields, AssetPreview& preview) {
    fields.attempted = true;
    const auto id = fields.prompt.resource_id(session.project_root(),
                                              *session.manifest());
    if (!session.import_svg(fields.prompt.source,
                            id,
                            fields.prompt.name)) {
        return false;
    }
    upload_preview(preview, session.imported_vector()->preview);
    preview.kind = PreviewKind::vector;
    return true;
}

void clear_asset_preview(AssetPreview& preview) {
    if (preview.texture != 0U) {
        glDeleteTextures(1, &preview.texture);
        preview.texture = 0U;
    }
    preview.width = 0;
    preview.height = 0;
    preview.kind = PreviewKind::none;
}

void draw_bounded_preview(const AssetPreview& preview, const float max_width,
                          const float max_height) {
    if (preview.texture == 0U || preview.width == 0U || preview.height == 0U) {
        ImGui::TextDisabled("Preview unavailable");
        return;
    }
    const float scale = std::min(
        max_width / static_cast<float>(preview.width),
        max_height / static_cast<float>(preview.height));
    const ImVec2 size{static_cast<float>(preview.width) * scale,
                      static_cast<float>(preview.height) * scale};
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.texture)), size,
                 {0.0F, 1.0F}, {1.0F, 0.0F});
}

void copy_path_to_buffer(const std::filesystem::path& path,
                         std::array<char, 1024>& buffer) {
    const std::string value = path.string();
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}

template <std::size_t Size>
bool choose_folder(SDL_Window* window, std::array<char, Size>& destination,
                   std::string& status) {
    nfdu8char_t* selected_path = nullptr;
    nfdpickfolderu8args_t arguments{};
    NFD_GetNativeWindowFromSDLWindow(window, &arguments.parentWindow);
    const nfdresult_t result = NFD_PickFolderU8_With(
        &selected_path, &arguments);
    if (result == NFD_CANCEL) {
        return false;
    }
    if (result == NFD_ERROR) {
        status = "Native folder dialog failed: " +
            std::string(NFD_GetError() == nullptr ? "unknown error"
                                                   : NFD_GetError());
        return false;
    }
    copy_path_to_buffer(std::filesystem::path{selected_path}, destination);
    NFD_FreePathU8(selected_path);
    return true;
}

template <std::size_t Size>
bool choose_asset_file(SDL_Window* window, const char* label,
                       const char* extension,
                       std::array<char, Size>& destination,
                       std::string& status) {
    nfdu8char_t* selected_path = nullptr;
    const nfdu8filteritem_t filters[]{{label, extension}};
    nfdopendialogu8args_t arguments{};
    arguments.filterList = filters;
    arguments.filterCount = 1;
    NFD_GetNativeWindowFromSDLWindow(window, &arguments.parentWindow);
    const nfdresult_t result = NFD_OpenDialogU8_With(
        &selected_path, &arguments);
    if (result == NFD_CANCEL) {
        return false;
    }
    if (result == NFD_ERROR) {
        status = "Native file dialog failed: " +
            std::string(NFD_GetError() == nullptr ? "unknown error"
                                                   : NFD_GetError());
        return false;
    }
    copy_path_to_buffer(std::filesystem::path{selected_path}, destination);
    NFD_FreePathU8(selected_path);
    return true;
}

void apply_studio_style() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 5.0F;
    style.ChildRounding = 4.0F;
    style.FrameRounding = 4.0F;
    style.TabRounding = 4.0F;
    style.WindowPadding = {10.0F, 10.0F};

    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = {0.055F, 0.063F, 0.078F, 1.0F};
    colors[ImGuiCol_TitleBg] = {0.075F, 0.086F, 0.105F, 1.0F};
    colors[ImGuiCol_TitleBgActive] = {0.11F, 0.13F, 0.16F, 1.0F};
    colors[ImGuiCol_Header] = {0.20F, 0.36F, 0.40F, 1.0F};
    colors[ImGuiCol_HeaderHovered] = {0.25F, 0.47F, 0.51F, 1.0F};
    colors[ImGuiCol_Button] = {0.20F, 0.36F, 0.40F, 1.0F};
    colors[ImGuiCol_ButtonHovered] = {0.25F, 0.47F, 0.51F, 1.0F};
    colors[ImGuiCol_CheckMark] = {0.89F, 0.68F, 0.34F, 1.0F};
}

void draw_project_tree(fabric::editor::ProjectSession& session,
                       AssetPreview& preview, std::string& status) {
    if (!session.has_project()) {
        ImGui::TextDisabled("No project open");
        ImGui::Spacing();
        ImGui::TextWrapped("Create or open a Vertex Loom project to begin.");
        return;
    }

    static ImGuiTextFilter filter;
    filter.Draw("Search", -1.0F);
    ImGui::Spacing();
    const auto draw_kind = [&](const char* label,
                               const fabric::editor::StudioResourceKind kind) {
        ImGui::SeparatorText(label);
        bool any = false;
        for (const auto& resource : session.resources()) {
            if (resource.kind != kind ||
                !filter.PassFilter(resource.name.c_str(),
                                   resource.id.value.c_str())) {
                continue;
            }
            any = true;
            const auto* selected = session.selected_resource();
            const bool is_selected = selected != nullptr &&
                selected->kind == resource.kind && selected->id == resource.id;
            const std::string item_label = resource.name + "##" +
                resource.id.value;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                if (session.select_resource(resource.kind, resource.id)) {
                    if (session.imported_texture()) {
                        upload_preview(preview,
                                       session.imported_texture()->image);
                        preview.kind = PreviewKind::texture;
                    } else if (session.imported_vector()) {
                        upload_preview(preview,
                                       session.imported_vector()->preview);
                        preview.kind = PreviewKind::vector;
                    } else {
                        clear_asset_preview(preview);
                    }
                    status = "Selected: " + resource.name;
                } else {
                    status = "Resource could not be loaded; inspect diagnostics.";
                }
            }
            if (resource.native) {
                ImGui::SameLine();
                ImGui::TextDisabled("native");
            }
        }
        if (!any) {
            ImGui::TextDisabled("None");
        }
    };
    draw_kind("Textures", fabric::editor::StudioResourceKind::texture);
    draw_kind("Vector artworks", fabric::editor::StudioResourceKind::vector);
}

void draw_diagnostics(const fabric::editor::ProjectSession& session) {
    if (session.errors().empty()) {
        ImGui::TextDisabled("Validation diagnostics will appear here.");
        return;
    }

    for (const auto& error : session.errors()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("%s - %s",
                           std::string(fabric::project::to_string(error.code)).c_str(),
                           error.field.c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", error.message.c_str());
        ImGui::Separator();
    }
}

void draw_prompt_error(const fabric::editor::PromptValidation& validation,
                       const std::string_view field) {
    if (const auto error = validation.error_for(field)) {
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s",
                           std::string(*error).c_str());
    }
}

void draw_prompt_summary(const fabric::editor::PromptValidation& validation) {
    ImGui::SeparatorText("Review");
    for (const auto& line : validation.summary) {
        ImGui::TextWrapped("%s", line.c_str());
    }
}

ImU32 color_to_u32(const fabric::core::Color& color) {
    const auto channel = [](const float value) {
        return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
    };
    return IM_COL32(channel(color.red), channel(color.green),
                    channel(color.blue), channel(color.alpha));
}

void draw_native_vector_canvas(const fabric::project::VectorAsset& asset,
                               CanvasUiState& canvas, const ImVec2 available) {
    if (!asset.native || asset.native->size.x <= 0.0F ||
        asset.native->size.y <= 0.0F) {
        ImGui::TextDisabled("Native artwork has no drawable canvas.");
        return;
    }
    ImGui::InvisibleButton("Native canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImVec2 center{origin.x + available.x * 0.5F,
                        origin.y + available.y * 0.5F};
    const float fit = std::min((available.x - 80.0F) / asset.native->size.x,
                               (available.y - 80.0F) / asset.native->size.y);
    const bool hovered = ImGui::IsItemHovered();
    auto& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.0F) {
        const float old_scale = fit * canvas.zoom;
        const ImVec2 mouse = io.MousePos;
        const ImVec2 world_under_cursor{
            (mouse.x - center.x - canvas.pan.x) / old_scale,
            -(mouse.y - center.y - canvas.pan.y) / old_scale};
        canvas.zoom = std::clamp(
            canvas.zoom * (io.MouseWheel > 0.0F ? 1.15F : 1.0F / 1.15F),
            0.1F, 20.0F);
        const float new_scale = fit * canvas.zoom;
        canvas.pan.x = mouse.x - center.x - world_under_cursor.x * new_scale;
        canvas.pan.y = mouse.y - center.y + world_under_cursor.y * new_scale;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        canvas.pan.x += delta.x;
        canvas.pan.y += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }
    const float pixels_per_unit = fit * canvas.zoom;
    const auto to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{center.x + canvas.pan.x + point.x * pixels_per_unit,
                      center.y + canvas.pan.y - point.y * pixels_per_unit};
    };
    auto* draw_list = ImGui::GetWindowDrawList();
    for (std::size_t node_index = 0;
         node_index < asset.native->nodes.size(); ++node_index) {
        const auto& node = asset.native->nodes[node_index];
        if (!node.visible) {
            continue;
        }
        const auto transform_point = [&](const fabric::core::Vec2 point) {
            const float x = (point.x - node.transform.pivot.x) *
                node.transform.scale.x;
            const float y = (point.y - node.transform.pivot.y) *
                node.transform.scale.y;
            const float angle = node.transform.rotation_degrees *
                std::numbers::pi_v<float> / 180.0F;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            return fabric::core::Vec2{
                node.transform.position.x + node.transform.pivot.x +
                    x * cosine - y * sine,
                node.transform.position.y + node.transform.pivot.y +
                    x * sine + y * cosine};
        };
        const auto& bounds = node.shape.bounds;
        std::vector<ImVec2> points;
        if (node.shape.kind == fabric::project::VectorShapeKind::ellipse) {
            constexpr int segments = 64;
            points.reserve(segments);
            const fabric::core::Vec2 ellipse_center{
                bounds.origin.x + bounds.size.x * 0.5F,
                bounds.origin.y + bounds.size.y * 0.5F};
            for (int segment = 0; segment < segments; ++segment) {
                const float angle = 2.0F * std::numbers::pi_v<float> *
                    static_cast<float>(segment) / static_cast<float>(segments);
                points.push_back(to_screen(transform_point({
                    ellipse_center.x + std::cos(angle) * bounds.size.x * 0.5F,
                    ellipse_center.y + std::sin(angle) * bounds.size.y * 0.5F})));
            }
        } else {
            points = {
                to_screen(transform_point(bounds.origin)),
                to_screen(transform_point({bounds.origin.x + bounds.size.x,
                                           bounds.origin.y})),
                to_screen(transform_point({bounds.origin.x + bounds.size.x,
                                           bounds.origin.y + bounds.size.y})),
                to_screen(transform_point({bounds.origin.x,
                                           bounds.origin.y + bounds.size.y})),
            };
        }
        fabric::core::Color fill{0.35F, 0.55F, 0.58F, 1.0F};
        if (node.fill.kind == fabric::project::VectorFillKind::solid &&
            node.fill.color) {
            fill = *node.fill.color;
        } else if (node.fill.kind == fabric::project::VectorFillKind::none) {
            fill.alpha = 0.0F;
        } else if (node.fill.kind == fabric::project::VectorFillKind::image) {
            fill = {0.89F, 0.68F, 0.34F, 0.8F};
        }
        if (fill.alpha > 0.0F) {
            draw_list->AddConvexPolyFilled(points.data(),
                                           static_cast<int>(points.size()),
                                           color_to_u32(fill));
        }
        const bool selected = node_index == canvas.selected_node;
        draw_list->AddPolyline(
            points.data(), static_cast<int>(points.size()),
            selected ? IM_COL32(236, 180, 75, 255)
                     : IM_COL32(225, 230, 235, 255),
            ImDrawFlags_Closed, selected ? 2.5F : 1.5F);
    }
    if (hovered) {
        ImGui::SetTooltip("Middle drag: pan  |  Wheel: zoom %.0f%%",
                          canvas.zoom * 100.0F);
    }
}

void draw_workspace(fabric::editor::ProjectSession& session,
                    SDL_Window* window,
                    std::array<char, 1024>& path_buffer,
                    CreationUiState& creation,
                    ImportUiState& imports,
                    AssetPreview& preview,
                    AssetPreview& pending_import_preview,
                    CanvasUiState& canvas,
                    ProjectSettingsUiState& project_settings,
                    bool& request_open,
                    bool& request_png,
                    bool& request_svg,
                    fabric::editor::SessionTransitionGuard& transition_guard,
                    bool& running,
                    std::string& status) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menu_height = ImGui::GetFrameHeight();
    const float status_height = 34.0F;
    const float left_width = std::clamp(viewport->Size.x * 0.22F, 240.0F, 330.0F);
    const float right_width = std::clamp(viewport->Size.x * 0.24F, 270.0F, 360.0F);
    const float content_height = viewport->Size.y - menu_height - status_height;

    ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({left_width, content_height});
    ImGui::Begin("Project", nullptr, fixed_panel_flags);
    draw_project_tree(session, preview, status);
    if (!session.has_project()) {
        ImGui::Spacing();
        if (ImGui::Button("Create project", {-1.0F, 0.0F})) {
            creation.request_project = true;
        }
        ImGui::SeparatorText("Open");
        if (ImGui::Button("Open project", {-1.0F, 0.0F})) {
            request_open = true;
        }
    } else {
        ImGui::Spacing();
        if (ImGui::Button("+ Add resource...", {-1.0F, 0.0F})) {
            ImGui::OpenPopup("Add resource");
        }
        if (ImGui::BeginPopup("Add resource")) {
            if (ImGui::MenuItem("New vector artwork...")) {
                creation.request_artwork = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import PNG texture...")) {
                imports.png.attempted = false;
                request_png = true;
            }
            if (ImGui::MenuItem("Import linked SVG...")) {
                imports.svg.attempted = false;
                request_svg = true;
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos({viewport->Pos.x + left_width,
                             viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({viewport->Size.x - left_width - right_width,
                              content_height});
    ImGui::Begin("Preview", nullptr, fixed_panel_flags);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(origin, {origin.x + available.x, origin.y + available.y},
                             IM_COL32(21, 24, 30, 255), 4.0F);
    constexpr float grid = 32.0F;
    for (float x = origin.x; x < origin.x + available.x; x += grid) {
        draw_list->AddLine({x, origin.y}, {x, origin.y + available.y},
                           IM_COL32(43, 48, 58, 120));
    }
    for (float y = origin.y; y < origin.y + available.y; y += grid) {
        draw_list->AddLine({origin.x, y}, {origin.x + available.x, y},
                           IM_COL32(43, 48, 58, 120));
    }
    const bool native_selected = session.selected_resource() != nullptr &&
        session.selected_resource()->kind ==
            fabric::editor::StudioResourceKind::vector &&
        session.selected_resource()->native && session.created_vector();
    if (native_selected) {
        draw_native_vector_canvas(*session.created_vector(), canvas, available);
    } else if (preview.texture != 0U) {
        const float image_width = static_cast<float>(preview.width);
        const float image_height = static_cast<float>(preview.height);
        const float scale = std::min((available.x - 40.0F) / image_width,
                                     (available.y - 40.0F) / image_height);
        const ImVec2 image_size{image_width * scale, image_height * scale};
        ImGui::SetCursorScreenPos({origin.x + (available.x - image_size.x) * 0.5F,
                                   origin.y + (available.y - image_size.y) * 0.5F});
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.texture)),
                     image_size, {0.0F, 1.0F}, {1.0F, 0.0F});
    } else {
        const char* preview_message = session.has_project()
                                          ? "Import a PNG or SVG to begin"
                                          : "Open a project to begin";
        const ImVec2 text_size = ImGui::CalcTextSize(preview_message);
        draw_list->AddText({origin.x + (available.x - text_size.x) * 0.5F,
                            origin.y + (available.y - text_size.y) * 0.5F},
                           IM_COL32(158, 170, 180, 255), preview_message);
        ImGui::Dummy(available);
    }
    ImGui::End();

    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - right_width,
                             viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({right_width, content_height});
    ImGui::Begin("Inspector", nullptr, fixed_panel_flags);
    if (session.has_project()) {
        const auto* selected = session.selected_resource();
        if (selected != nullptr) {
            ImGui::TextUnformatted(selected->name.c_str());
            ImGui::TextDisabled("%s", selected->id.value.c_str());
            ImGui::TextDisabled("%s",
                                selected->document_path.generic_string().c_str());
        } else {
            ImGui::TextDisabled("Select a resource to inspect it.");
        }
        if (preview.texture != 0U) {
            ImGui::SeparatorText(preview.kind == PreviewKind::vector
                                     ? "Imported vector"
                                     : "Imported texture");
            ImGui::Text("%u x %u RGBA8", preview.width, preview.height);
            if (preview.kind == PreviewKind::texture &&
                session.imported_texture()) {
                ImGui::TextUnformatted(
                    session.imported_texture()->asset.document.name.c_str());
                ImGui::TextDisabled("%s",
                    session.imported_texture()->asset.document.id.value.c_str());
            }
            if (preview.kind == PreviewKind::texture &&
                session.imported_texture()) {
                ImGui::TextWrapped("%s",
                    session.imported_texture()->asset.source.generic_string().c_str());
            }
            if (preview.kind == PreviewKind::vector && session.imported_vector()) {
                ImGui::TextUnformatted(
                    session.imported_vector()->asset.document.name.c_str());
                ImGui::TextDisabled("%s",
                    session.imported_vector()->asset.document.id.value.c_str());
                ImGui::TextWrapped("%s",
                    session.imported_vector()->asset.source.generic_string().c_str());
            }
        }
        if (creation.prepared_artwork && selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::vector &&
            selected->native) {
            ImGui::SeparatorText("Created native artwork");
            ImGui::TextUnformatted(creation.prepared_artwork->name.c_str());
            if (session.created_vector()) {
                ImGui::TextDisabled(
                    "%s", session.created_vector()->document.id.value.c_str());
            }
            ImGui::Text("%.2f x %.2f world units",
                        creation.prepared_artwork->width,
                        creation.prepared_artwork->height);
            ImGui::TextDisabled("Published as VectorAsset v2 native.");
        }
        if (selected != nullptr && selected->native && session.created_vector() &&
            session.created_vector()->native &&
            !session.created_vector()->native->nodes.empty()) {
            ImGui::SeparatorText("Nodes");
            const auto& nodes = session.created_vector()->native->nodes;
            canvas.selected_node = std::min(canvas.selected_node,
                                            nodes.size() - 1);
            for (std::size_t node_index = 0; node_index < nodes.size();
                 ++node_index) {
                const std::string label = nodes[node_index].name + "##node-" +
                    std::to_string(node_index);
                if (ImGui::Selectable(label.c_str(),
                                      canvas.selected_node == node_index)) {
                    canvas.selected_node = node_index;
                }
            }
            ImGui::SeparatorText("Node properties");
            auto node = nodes[canvas.selected_node];
            const auto commit_node = [&](fabric::project::VectorNode changed) {
                if (session.set_selected_vector_node(
                        canvas.selected_node, std::move(changed))) {
                    status = "Vector node changed.";
                } else {
                    status = "Vector change rejected; inspect diagnostics.";
                }
            };
            bool locked = node.locked;
            if (ImGui::Checkbox("Locked", &locked)) {
                node.locked = locked;
                commit_node(node);
            }
            ImGui::BeginDisabled(node.locked);
            std::string node_name = node.name;
            if (ImGui::InputText("Name", &node_name)) {
                node.name = std::move(node_name);
                commit_node(node);
            }
            bool visible = node.visible;
            if (ImGui::Checkbox("Visible", &visible)) {
                node.visible = visible;
                commit_node(node);
            }
            float position[]{node.transform.position.x,
                             node.transform.position.y};
            if (ImGui::InputFloat2("Position", position)) {
                node.transform.position = {position[0], position[1]};
                commit_node(node);
            }
            float scale[]{node.transform.scale.x, node.transform.scale.y};
            if (ImGui::InputFloat2("Scale", scale)) {
                node.transform.scale = {scale[0], scale[1]};
                commit_node(node);
            }
            float rotation = node.transform.rotation_degrees;
            if (ImGui::InputFloat("Rotation", &rotation, 1.0F, 10.0F,
                                  "%.2f deg")) {
                node.transform.rotation_degrees = rotation;
                commit_node(node);
            }
            if (node.fill.kind == fabric::project::VectorFillKind::solid &&
                node.fill.color) {
                float color[]{node.fill.color->red, node.fill.color->green,
                              node.fill.color->blue, node.fill.color->alpha};
                if (ImGui::ColorEdit4("Fill", color)) {
                    node.fill.color = fabric::core::Color{
                        color[0], color[1], color[2], color[3]};
                    commit_node(node);
                }
            } else if (node.fill.kind ==
                           fabric::project::VectorFillKind::image &&
                       node.fill.image) {
                ImGui::Text("Image fill: %s",
                            node.fill.image->texture.id.value.c_str());
                auto fit = node.fill.image->fit;
                const auto fit_label =
                    std::string(fabric::project::to_string(fit));
                if (ImGui::BeginCombo("Fit", fit_label.c_str())) {
                    for (const auto option : {
                             fabric::project::VectorImageFit::contain,
                             fabric::project::VectorImageFit::cover,
                             fabric::project::VectorImageFit::stretch,
                             fabric::project::VectorImageFit::free}) {
                        const bool selected_fit = option == fit;
                        const auto label =
                            std::string(fabric::project::to_string(option));
                        if (ImGui::Selectable(label.c_str(), selected_fit)) {
                            node.fill.image->fit = option;
                            commit_node(node);
                        }
                    }
                    ImGui::EndCombo();
                }
                float image_offset[]{
                    node.fill.image->transform.position.x,
                    node.fill.image->transform.position.y};
                if (ImGui::InputFloat2("Image offset", image_offset)) {
                    node.fill.image->transform.position = {
                        image_offset[0], image_offset[1]};
                    commit_node(node);
                }
                float image_scale[]{node.fill.image->transform.scale.x,
                                    node.fill.image->transform.scale.y};
                if (ImGui::InputFloat2("Image scale", image_scale)) {
                    node.fill.image->transform.scale = {
                        image_scale[0], image_scale[1]};
                    commit_node(node);
                }
                float image_rotation =
                    node.fill.image->transform.rotation_degrees;
                if (ImGui::InputFloat("Image rotation", &image_rotation,
                                      1.0F, 10.0F, "%.2f deg")) {
                    node.fill.image->transform.rotation_degrees = image_rotation;
                    commit_node(node);
                }
                float opacity = node.fill.image->opacity;
                if (ImGui::SliderFloat("Image opacity", &opacity, 0.0F, 1.0F,
                                       "%.2f")) {
                    node.fill.image->opacity = opacity;
                    commit_node(node);
                }
                bool deform = node.fill.image->deform_with_shape;
                if (ImGui::Checkbox("Deform with shape", &deform)) {
                    node.fill.image->deform_with_shape = deform;
                    commit_node(node);
                }
            }
            ImGui::EndDisabled();
        }
    } else {
        ImGui::TextDisabled("No selection");
    }
    if (!session.errors().empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Diagnostics");
        draw_diagnostics(session);
    }
    ImGui::End();

    if (project_settings.request && session.has_project()) {
        project_settings.name = session.manifest()->name;
        project_settings.pixels_per_unit = session.manifest()->pixels_per_unit;
        ImGui::OpenPopup("Project settings");
        project_settings.request = false;
    }
    if (ImGui::BeginPopupModal("Project settings", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0F);
        ImGui::InputText("Project name", &project_settings.name);
        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputDouble("Pixels per unit",
                           &project_settings.pixels_per_unit, 1.0, 10.0,
                           "%.2f");
        ImGui::TextDisabled("%s", session.project_root().string().c_str());
        const bool valid = !project_settings.name.empty() &&
            project_settings.name.size() <= 255 &&
            std::isfinite(project_settings.pixels_per_unit) &&
            project_settings.pixels_per_unit > 0.0 &&
            project_settings.pixels_per_unit <= 1'000'000.0;
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Apply", {110.0F, 0.0F})) {
            const bool name_updated =
                session.set_project_name(project_settings.name);
            const bool units_updated = session.set_pixels_per_unit(
                project_settings.pixels_per_unit);
            if (name_updated && units_updated) {
                status = "Project settings changed.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Invalid project settings; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos({viewport->Pos.x,
                             viewport->Pos.y + viewport->Size.y - status_height});
    ImGui::SetNextWindowSize({viewport->Size.x, status_height});
    ImGui::Begin("Status", nullptr, fixed_panel_flags | ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted(status.c_str());
    if (session.dirty()) {
        ImGui::SameLine();
        ImGui::TextColored({0.89F, 0.68F, 0.34F, 1.0F}, "Unsaved changes");
    }
    ImGui::End();

    if (creation.request_project) {
        creation.project.reset();
        creation.project_publish_attempted = false;
        ImGui::OpenPopup("Create project");
        creation.request_project = false;
    }
    if (creation.request_artwork && session.has_project()) {
        creation.artwork.reset();
        ImGui::OpenPopup("Create vector artwork");
        creation.request_artwork = false;
    }
    if (request_open) {
        if (choose_folder(window, path_buffer, status)) {
            if (session.open(path_buffer.data())) {
                clear_asset_preview(preview);
                creation.prepared_artwork.reset();
                status = "Project opened: " + session.manifest()->name;
            } else {
                status = "Project rejected; inspect the diagnostics.";
            }
        }
        request_open = false;
    }
    if (request_png) {
        imports.png = {};
        clear_asset_preview(pending_import_preview);
        std::array<char, 1024> selected{};
        if (choose_asset_file(window, "PNG image", "png", selected, status)) {
            imports.png.prompt.source = selected.data();
            imports.png.prompt.name =
                imports.png.prompt.source.stem().string();
            auto decoded = fabric::render::load_png(imports.png.prompt.source);
            if (decoded.ok()) {
                upload_preview(pending_import_preview, *decoded.image);
                pending_import_preview.kind = PreviewKind::texture;
                ImGui::OpenPopup("Import PNG");
            } else {
                status = "PNG preview failed: " + decoded.error->message;
            }
        }
        request_png = false;
    }
    if (request_svg) {
        imports.svg = {};
        clear_asset_preview(pending_import_preview);
        std::array<char, 1024> selected{};
        if (choose_asset_file(window, "SVG image", "svg", selected, status)) {
            imports.svg.prompt.source = selected.data();
            imports.svg.prompt.name =
                imports.svg.prompt.source.stem().string();
            auto decoded = fabric::render::load_svg_preview(
                imports.svg.prompt.source);
            if (decoded.ok()) {
                upload_preview(pending_import_preview, *decoded.image);
                pending_import_preview.kind = PreviewKind::vector;
                ImGui::OpenPopup("Import SVG");
            } else {
                status = "SVG preview failed: " + decoded.error->message;
            }
        }
        request_svg = false;
    }
    if (ImGui::BeginPopupModal("Import PNG", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("PNG source file");
        ImGui::TextWrapped("%s", imports.png.prompt.source.string().c_str());
        draw_bounded_preview(pending_import_preview, 560.0F, 220.0F);
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &imports.png.prompt.name);
        ImGui::TextDisabled("The PNG and its versioned document are copied into assets/textures.");
        const auto validation = imports.png.prompt.validate(
            fabric::editor::ImportSourceKind::png_image,
            session.project_root(), *session.manifest());
        draw_prompt_error(validation, "source");
        draw_prompt_error(validation, "name");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Import", {110.0F, 0.0F})) {
            if (import_texture(session, imports.png, preview)) {
                status = "PNG imported: " +
                    session.imported_texture()->asset.document.id.value;
                imports.png = {};
                clear_asset_preview(pending_import_preview);
                ImGui::CloseCurrentPopup();
            } else {
                status = "PNG import failed; inspect the diagnostics.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            imports.png = {};
            clear_asset_preview(pending_import_preview);
            ImGui::CloseCurrentPopup();
        }
        if (imports.png.attempted && !session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Import failed");
            draw_diagnostics(session);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Import SVG", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("SVG source file");
        ImGui::TextWrapped("%s", imports.svg.prompt.source.string().c_str());
        draw_bounded_preview(pending_import_preview, 560.0F, 220.0F);
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &imports.svg.prompt.name);
        ImGui::TextDisabled(
            "The SVG and its versioned document are copied into assets/vectors.");
        const auto validation = imports.svg.prompt.validate(
            fabric::editor::ImportSourceKind::linked_svg,
            session.project_root(), *session.manifest());
        draw_prompt_error(validation, "source");
        draw_prompt_error(validation, "name");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Import", {110.0F, 0.0F})) {
            if (import_vector(session, imports.svg, preview)) {
                status = "SVG imported: " +
                    session.imported_vector()->asset.document.id.value;
                imports.svg = {};
                clear_asset_preview(pending_import_preview);
                ImGui::CloseCurrentPopup();
            } else {
                status = "SVG import failed; inspect the diagnostics.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            imports.svg = {};
            clear_asset_preview(pending_import_preview);
            ImGui::CloseCurrentPopup();
        }
        if (imports.svg.attempted && !session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Import failed");
            draw_diagnostics(session);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create project", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a versioned Vertex Loom project");
        ImGui::Spacing();
        std::string destination = creation.project.parent_directory.string();
        ImGui::SetNextItemWidth(560.0F);
        if (ImGui::InputText("Parent folder", &destination)) {
            creation.project.parent_directory = destination;
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::array<char, 1024> selected{};
            if (choose_folder(window, selected, status)) {
                creation.project.parent_directory = selected.data();
            }
        }
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.project.name);
        const auto preset_label = std::string(fabric::editor::label(
            creation.project.preset));
        ImGui::SetNextItemWidth(300.0F);
        if (ImGui::BeginCombo("Project preset", preset_label.c_str())) {
            for (const auto preset : {
                     fabric::editor::ProjectScalePreset::standard,
                     fabric::editor::ProjectScalePreset::compact,
                     fabric::editor::ProjectScalePreset::high_detail,
                     fabric::editor::ProjectScalePreset::custom}) {
                const bool selected = creation.project.preset == preset;
                const auto option = std::string(fabric::editor::label(preset));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.project.select_preset(preset);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted("Units: world units");
        ImGui::SetNextItemWidth(220.0F);
        if (ImGui::InputDouble("Pixels per unit",
                               &creation.project.pixels_per_unit, 1.0, 10.0,
                               "%.2f")) {
            creation.project.preset =
                fabric::editor::ProjectScalePreset::custom;
        }
        const auto validation = creation.project.validate();
        draw_prompt_error(validation, "destination");
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "pixelsPerUnit");
        draw_prompt_summary(validation);
        ImGui::Spacing();
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create project", {140.0F, 0.0F})) {
            creation.project_publish_attempted = true;
            if (session.create(creation.project.project_root(),
                               creation.project.manifest())) {
                clear_asset_preview(preview);
                creation.prepared_artwork.reset();
                status = "Project created: " + session.manifest()->name;
                copy_path_to_buffer(session.project_root(), path_buffer);
                ImGui::CloseCurrentPopup();
            } else {
                status = "Project creation failed; inspect the diagnostics.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.project.reset();
            creation.project_publish_attempted = false;
            ImGui::CloseCurrentPopup();
        }
        if (creation.project_publish_attempted && !session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Creation failed");
            draw_diagnostics(session);
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints({640.0F, 300.0F},
                                        {700.0F, viewport->Size.y - 80.0F});
    if (ImGui::BeginPopupModal("Create vector artwork", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a native, editable vector artwork");
        ImGui::TextDisabled("The validated document is published atomically in the open project.");
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.artwork.name);
        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputDouble("Width", &creation.artwork.width, 1.0, 10.0,
                           "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputDouble("Height", &creation.artwork.height, 1.0, 10.0,
                           "%.2f");
        ImGui::TextUnformatted("Units: project world units");
        const auto origin_label =
            std::string(fabric::editor::label(creation.artwork.origin));
        if (ImGui::BeginCombo("Origin", origin_label.c_str())) {
            for (const auto origin : {fabric::editor::ArtworkOrigin::center,
                                      fabric::editor::ArtworkOrigin::top_left}) {
                const bool selected = creation.artwork.origin == origin;
                const auto option = std::string(fabric::editor::label(origin));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.artwork.origin = origin;
                }
            }
            ImGui::EndCombo();
        }
        const auto shape_label =
            std::string(fabric::editor::label(creation.artwork.first_shape));
        if (ImGui::BeginCombo("First shape", shape_label.c_str())) {
            for (const auto shape : {fabric::editor::InitialShape::rectangle,
                                     fabric::editor::InitialShape::ellipse,
                                     fabric::editor::InitialShape::empty}) {
                const bool selected = creation.artwork.first_shape == shape;
                const auto option = std::string(fabric::editor::label(shape));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.artwork.first_shape = shape;
                }
            }
            ImGui::EndCombo();
        }
        const auto fill_label =
            std::string(fabric::editor::label(creation.artwork.initial_fill));
        if (ImGui::BeginCombo("Initial fill", fill_label.c_str())) {
            for (const auto fill : {fabric::editor::InitialFill::color,
                                    fabric::editor::InitialFill::image,
                                    fabric::editor::InitialFill::transparent}) {
                const bool selected = creation.artwork.initial_fill == fill;
                const auto option = std::string(fabric::editor::label(fill));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.artwork.initial_fill = fill;
                }
            }
            ImGui::EndCombo();
        }
        if (creation.artwork.initial_fill ==
            fabric::editor::InitialFill::color) {
            float color[] = {creation.artwork.initial_color.red,
                             creation.artwork.initial_color.green,
                             creation.artwork.initial_color.blue,
                             creation.artwork.initial_color.alpha};
            if (ImGui::ColorEdit4("Initial color", color)) {
                creation.artwork.initial_color = {
                    color[0], color[1], color[2], color[3]};
            }
        } else if (creation.artwork.initial_fill ==
                   fabric::editor::InitialFill::image) {
            const auto chosen_texture = std::ranges::find_if(
                session.resources(), [&](const fabric::editor::StudioResource& resource) {
                    return resource.kind ==
                               fabric::editor::StudioResourceKind::texture &&
                        resource.id.value == creation.artwork.initial_image_id;
                });
            const char* chosen_label = chosen_texture == session.resources().end()
                ? "Choose a texture..."
                : chosen_texture->name.c_str();
            ImGui::SetNextItemWidth(360.0F);
            if (ImGui::BeginCombo("Texture", chosen_label)) {
                for (const auto& resource : session.resources()) {
                    if (resource.kind !=
                        fabric::editor::StudioResourceKind::texture) {
                        continue;
                    }
                    const bool selected = resource.id.value ==
                        creation.artwork.initial_image_id;
                    if (ImGui::Selectable(resource.name.c_str(), selected)) {
                        creation.artwork.initial_image_id = resource.id.value;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            const auto fit_label = std::string(fabric::project::to_string(
                creation.artwork.image_fit));
            if (ImGui::BeginCombo("Image fit", fit_label.c_str())) {
                for (const auto fit : {
                         fabric::project::VectorImageFit::contain,
                         fabric::project::VectorImageFit::cover,
                         fabric::project::VectorImageFit::stretch,
                         fabric::project::VectorImageFit::free}) {
                    const bool selected = creation.artwork.image_fit == fit;
                    const auto option =
                        std::string(fabric::project::to_string(fit));
                    if (ImGui::Selectable(option.c_str(), selected)) {
                        creation.artwork.image_fit = fit;
                    }
                }
                ImGui::EndCombo();
            }
            float offset[] = {
                creation.artwork.image_transform.position.x,
                creation.artwork.image_transform.position.y};
            if (ImGui::InputFloat2("Image offset", offset)) {
                creation.artwork.image_transform.position = {
                    offset[0], offset[1]};
            }
            float scale[] = {creation.artwork.image_transform.scale.x,
                             creation.artwork.image_transform.scale.y};
            if (ImGui::InputFloat2("Image scale", scale)) {
                creation.artwork.image_transform.scale = {scale[0], scale[1]};
            }
            float pivot[] = {creation.artwork.image_transform.pivot.x,
                             creation.artwork.image_transform.pivot.y};
            if (ImGui::InputFloat2("Image pivot", pivot)) {
                creation.artwork.image_transform.pivot = {
                    pivot[0], pivot[1]};
            }
            ImGui::InputFloat(
                "Image rotation",
                &creation.artwork.image_transform.rotation_degrees,
                1.0F, 10.0F, "%.2f deg");
            float opacity = static_cast<float>(
                creation.artwork.image_opacity);
            if (ImGui::SliderFloat("Image opacity", &opacity, 0.0F, 1.0F,
                                   "%.2f")) {
                creation.artwork.image_opacity = opacity;
            }
            ImGui::Checkbox("Deform image with shape",
                            &creation.artwork.deform_image_with_shape);
        }
        const auto validation = creation.artwork.validate(
            session.project_root(), *session.manifest());
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "id");
        draw_prompt_error(validation, "width");
        draw_prompt_error(validation, "height");
        draw_prompt_error(validation, "initialFill");
        draw_prompt_error(validation, "initialImage");
        draw_prompt_error(validation, "imageTransform");
        draw_prompt_error(validation, "imageOpacity");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create artwork", {140.0F, 0.0F})) {
            if (session.create_vector_artwork(creation.artwork)) {
                creation.prepared_artwork = creation.artwork;
                clear_asset_preview(preview);
                canvas = {};
                status = "Native artwork created and saved.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Native artwork creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.artwork.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (session.has_recovery()) {
        ImGui::OpenPopup("Recover autosave");
    }
    if (ImGui::BeginPopupModal("Recover autosave", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("A newer valid autosave is available.");
        ImGui::TextWrapped(
            "Recover it in memory? The saved project will not be overwritten until Save.");
        if (ImGui::Button("Recover", {110.0F, 0.0F})) {
            if (session.accept_recovery()) {
                status = "Autosave recovered; save to keep it.";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep saved", {110.0F, 0.0F})) {
            session.decline_recovery();
            status = "Saved project kept.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const auto continue_session_action = [&](const fabric::editor::SessionAction action) {
        switch (action) {
        case fabric::editor::SessionAction::create_project:
            creation.request_project = true;
            break;
        case fabric::editor::SessionAction::open_project:
            request_open = true;
            break;
        case fabric::editor::SessionAction::quit:
            running = false;
            break;
        }
    };
    if (const auto ready = transition_guard.take_ready()) {
        continue_session_action(*ready);
    }
    if (transition_guard.confirmation_required()) {
        ImGui::OpenPopup("Unsaved changes");
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The current project has unsaved changes.");
        ImGui::TextWrapped(
            "Save them before replacing the project or closing Asset Studio?");
        if (ImGui::Button("Save and continue", {150.0F, 0.0F})) {
            if (session.save()) {
                status = "Project saved.";
                ImGui::CloseCurrentPopup();
                if (const auto ready = transition_guard.resolve(
                        fabric::editor::UnsavedDecision::save)) {
                    continue_session_action(*ready);
                }
            } else {
                static_cast<void>(transition_guard.resolve(
                    fabric::editor::UnsavedDecision::save, false));
                status = "Save failed; inspect the diagnostics.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
            if (const auto ready = transition_guard.resolve(
                    fabric::editor::UnsavedDecision::discard)) {
                continue_session_action(*ready);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            static_cast<void>(transition_guard.resolve(
                fabric::editor::UnsavedDecision::cancel));
            ImGui::CloseCurrentPopup();
            status = "Action cancelled; unsaved changes kept.";
        }
        ImGui::EndPopup();
    }

}

int run_asset_studio(const std::filesystem::path& initial_project) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "Vertex Loom - Asset Studio", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1440, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::cerr << "window creation failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 900, 600);

    if (NFD_Init() != NFD_OKAY) {
        std::cerr << "native file dialog initialization failed: "
                  << (NFD_GetError() == nullptr ? "unknown error"
                                                : NFD_GetError())
                  << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        std::cerr << "OpenGL context creation failed: " << SDL_GetError() << '\n';
        NFD_Quit();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imgui_io.IniFilename = nullptr;
    apply_studio_style();
    const bool sdl_backend_ready = ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    const bool opengl_backend_ready =
        sdl_backend_ready && ImGui_ImplOpenGL3_Init(glsl_version);
    if (!opengl_backend_ready) {
        std::cerr << "Dear ImGui backend initialization failed\n";
        if (sdl_backend_ready) {
            ImGui_ImplSDL2_Shutdown();
        }
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(gl_context);
        NFD_Quit();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    fabric::editor::ProjectSession session;
    std::array<char, 1024> path_buffer{};
    CreationUiState creation;
    ImportUiState imports;
    AssetPreview preview;
    AssetPreview pending_import_preview;
    CanvasUiState canvas;
    ProjectSettingsUiState project_settings;
    bool request_open = false;
    bool request_png = false;
    bool request_svg = false;
    fabric::editor::SessionTransitionGuard transition_guard;
    std::string status{"Ready"};
    if (!initial_project.empty()) {
        copy_path_to_buffer(initial_project, path_buffer);
        if (session.open(initial_project)) {
            status = "Project opened: " + session.manifest()->name;
        } else {
            status = "Project rejected; inspect the diagnostics.";
        }
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                transition_guard.request(fabric::editor::SessionAction::quit,
                                         session.dirty());
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New project...", new_shortcut)) {
                    transition_guard.request(
                        fabric::editor::SessionAction::create_project,
                        session.dirty());
                }
                if (ImGui::MenuItem("Open project...", open_shortcut)) {
                    if (session.has_project()) {
                        copy_path_to_buffer(session.project_root(), path_buffer);
                    }
                    transition_guard.request(
                        fabric::editor::SessionAction::open_project,
                        session.dirty());
                }
                if (ImGui::MenuItem("Save", save_shortcut, false,
                                    session.has_project())) {
                    status = session.save()
                        ? "Project saved."
                        : "Save failed; inspect the diagnostics.";
                }
                if (ImGui::MenuItem("Project settings...", nullptr, false,
                                    session.has_project())) {
                    project_settings.request = true;
                }
                if (ImGui::BeginMenu("Create", session.has_project())) {
                    if (ImGui::MenuItem("Vector artwork...")) {
                        creation.request_artwork = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Import", session.has_project())) {
                    if (ImGui::MenuItem("PNG image source...", import_shortcut)) {
                        request_png = true;
                    }
                    if (ImGui::MenuItem("Linked SVG source...",
                                        import_svg_shortcut)) {
                        request_svg = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", quit_shortcut)) {
                    transition_guard.request(fabric::editor::SessionAction::quit,
                                             session.dirty());
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", undo_shortcut, false,
                                    session.can_undo())) {
                    static_cast<void>(session.undo());
                    status = "Change undone.";
                }
                if (ImGui::MenuItem("Redo", redo_shortcut, false,
                                    session.can_redo())) {
                    static_cast<void>(session.redo());
                    status = "Change redone.";
                }
                ImGui::EndMenu();
            }
            ImGui::TextDisabled("Native vector resource workspace");
            ImGui::EndMainMenuBar();
        }
        const auto& io = ImGui::GetIO();
        #if defined(__APPLE__)
        const bool command_modifier = io.KeySuper;
        #else
        const bool command_modifier = io.KeyCtrl;
        #endif
        const bool shortcuts_enabled =
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            transition_guard.request(fabric::editor::SessionAction::open_project,
                                     session.dirty());
        }
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            transition_guard.request(
                fabric::editor::SessionAction::create_project,
                session.dirty());
        }
        if (shortcuts_enabled && command_modifier && session.has_project() &&
            ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            if (io.KeyShift) {
                request_svg = true;
            } else {
                request_png = true;
            }
        }
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            transition_guard.request(fabric::editor::SessionAction::quit,
                                     session.dirty());
        }
        if (shortcuts_enabled && command_modifier && session.has_project() &&
            ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            status = session.save()
                ? "Project saved."
                : "Save failed; inspect the diagnostics.";
        }
        if (shortcuts_enabled && command_modifier && !io.KeyShift &&
            ImGui::IsKeyPressed(ImGuiKey_Z, false) && session.can_undo()) {
            static_cast<void>(session.undo());
            status = "Change undone.";
        }
        if (shortcuts_enabled && command_modifier &&
            ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
             ImGui::IsKeyPressed(ImGuiKey_Y, false)) && session.can_redo()) {
            static_cast<void>(session.redo());
            status = "Change redone.";
        }

        draw_workspace(session, window, path_buffer, creation, imports, preview,
                       pending_import_preview, canvas, project_settings,
                       request_open, request_png, request_svg,
                       transition_guard, running, status);

        const auto autosave_status = session.update_autosave();
        if (autosave_status == fabric::editor::AutosaveStatus::saved) {
            status = "Recovery autosave updated.";
        } else if (autosave_status == fabric::editor::AutosaveStatus::failed) {
            status = "Autosave failed; inspect the diagnostics.";
        }

        ImGui::Render();
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        glViewport(0, 0, drawable_width, drawable_height);
        glClearColor(0.035F, 0.041F, 0.052F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    if (preview.texture != 0U) {
        glDeleteTextures(1, &preview.texture);
    }
    if (pending_import_preview.texture != 0U) {
        glDeleteTextures(1, &pending_import_preview.texture);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    NFD_Quit();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(const int argument_count, char** arguments) {
    if (argument_count > 2) {
        std::cerr << "usage: asset_studio [project-directory]\n";
        return 64;
    }
    const std::filesystem::path initial_project =
        argument_count == 2 ? std::filesystem::path{arguments[1]}
                            : std::filesystem::path{};
    return run_asset_studio(initial_project);
}
