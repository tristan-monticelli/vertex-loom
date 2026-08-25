#include "import_workflow.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <nfd.h>
#include <nfd_sdl2.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string_view>

namespace fabric::asset_studio {

namespace {

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

bool choose_asset_file(SDL_Window* window, const char* label,
                       const char* extension,
                       std::array<char, 1024>& destination,
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

void draw_diagnostics(const editor::ProjectSession& session) {
    for (const auto& error : session.errors()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("%s - %s",
                           std::string(project::to_string(error.code)).c_str(),
                           error.field.c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", error.message.c_str());
        ImGui::Separator();
    }
}

void draw_prompt_error(const editor::PromptValidation& validation,
                       const std::string_view field) {
    if (const auto error = validation.error_for(field)) {
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s",
                           std::string(*error).c_str());
    }
}

void draw_prompt_summary(const editor::PromptValidation& validation) {
    ImGui::SeparatorText("Review");
    for (const auto& line : validation.summary) {
        ImGui::TextWrapped("%s", line.c_str());
    }
}

void prepare_import(SDL_Window* window, SourceImportFields& fields,
                    AssetPreview& pending_preview, const char* label,
                    const char* extension, const PreviewKind kind,
                    const char* popup_name, std::string& status) {
    fields = {};
    clear_asset_preview(pending_preview);
    std::array<char, 1024> selected{};
    if (!choose_asset_file(window, label, extension, selected, status)) {
        return;
    }
    fields.prompt.source = selected.data();
    fields.prompt.name = fields.prompt.source.stem().string();
    const auto decoded = kind == PreviewKind::texture
        ? render::load_png(fields.prompt.source)
        : render::load_svg_preview(fields.prompt.source);
    if (!decoded.ok()) {
        status = std::string(label) + " preview failed: " +
            decoded.error->message;
        return;
    }
    upload_preview(pending_preview, *decoded.image);
    pending_preview.kind = kind;
    ImGui::OpenPopup(popup_name);
}

} // namespace

void upload_preview(AssetPreview& preview, const render::RasterImage& image) {
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

void clear_asset_preview(AssetPreview& preview) {
    if (preview.texture != 0U) {
        glDeleteTextures(1, &preview.texture);
        preview.texture = 0U;
    }
    preview.width = 0;
    preview.height = 0;
    preview.kind = PreviewKind::none;
}

bool import_texture(editor::ProjectSession& session,
                    SourceImportFields& fields, AssetPreview& preview) {
    fields.attempted = true;
    const auto id = fields.prompt.resource_id(session.project_root(),
                                              *session.manifest());
    if (!session.import_png(fields.prompt.source, id, fields.prompt.name)) {
        return false;
    }
    upload_preview(preview, session.imported_texture()->image);
    preview.kind = PreviewKind::texture;
    return true;
}

bool import_vector(editor::ProjectSession& session,
                   SourceImportFields& fields, AssetPreview& preview) {
    fields.attempted = true;
    const auto id = fields.prompt.resource_id(session.project_root(),
                                              *session.manifest());
    if (!session.import_svg(fields.prompt.source, id, fields.prompt.name)) {
        return false;
    }
    upload_preview(preview, session.imported_vector()->preview);
    preview.kind = PreviewKind::vector;
    return true;
}

void draw_import_workflow(editor::ProjectSession& session, SDL_Window* window,
                          ImportUiState& imports, AssetPreview& preview,
                          AssetPreview& pending_preview, bool& request_png,
                          bool& request_svg, std::string& status) {
    if (request_png) {
        prepare_import(window, imports.png, pending_preview, "PNG image", "png",
                       PreviewKind::texture, "Import PNG", status);
        request_png = false;
    }
    if (request_svg) {
        prepare_import(window, imports.svg, pending_preview, "SVG image", "svg",
                       PreviewKind::vector, "Import SVG", status);
        request_svg = false;
    }

    if (ImGui::BeginPopupModal("Import PNG", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("PNG source file");
        ImGui::TextWrapped("%s", imports.png.prompt.source.string().c_str());
        draw_bounded_preview(pending_preview, 560.0F, 220.0F);
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &imports.png.prompt.name);
        ImGui::TextDisabled("The PNG and its versioned document are copied into assets/textures.");
        const auto validation = imports.png.prompt.validate(
            editor::ImportSourceKind::png_image, session.project_root(),
            *session.manifest());
        draw_prompt_error(validation, "source");
        draw_prompt_error(validation, "name");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Import", {110.0F, 0.0F})) {
            if (import_texture(session, imports.png, preview)) {
                status = "PNG imported: " +
                    session.imported_texture()->asset.document.id.value;
                imports.png = {};
                clear_asset_preview(pending_preview);
                ImGui::CloseCurrentPopup();
            } else {
                status = "PNG import failed; inspect the diagnostics.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            imports.png = {};
            clear_asset_preview(pending_preview);
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
        draw_bounded_preview(pending_preview, 560.0F, 220.0F);
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &imports.svg.prompt.name);
        ImGui::TextDisabled("The SVG and its versioned document are copied into assets/vectors.");
        const auto validation = imports.svg.prompt.validate(
            editor::ImportSourceKind::linked_svg, session.project_root(),
            *session.manifest());
        draw_prompt_error(validation, "source");
        draw_prompt_error(validation, "name");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Import", {110.0F, 0.0F})) {
            if (import_vector(session, imports.svg, preview)) {
                status = "SVG imported: " +
                    session.imported_vector()->asset.document.id.value;
                imports.svg = {};
                clear_asset_preview(pending_preview);
                ImGui::CloseCurrentPopup();
            } else {
                status = "SVG import failed; inspect the diagnostics.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            imports.svg = {};
            clear_asset_preview(pending_preview);
            ImGui::CloseCurrentPopup();
        }
        if (imports.svg.attempted && !session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Import failed");
            draw_diagnostics(session);
        }
        ImGui::EndPopup();
    }
}

} // namespace fabric::asset_studio
