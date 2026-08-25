#pragma once

#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/render/raster_image.hpp"

#include <SDL_opengl.h>

#include <cstdint>
#include <string>

struct SDL_Window;

namespace fabric::asset_studio {

enum class PreviewKind {
    none,
    texture,
    vector,
};

struct SourceImportFields {
    editor::ImportSourcePrompt prompt;
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

void upload_preview(AssetPreview& preview, const render::RasterImage& image);
void clear_asset_preview(AssetPreview& preview);

bool import_texture(editor::ProjectSession& session,
                    SourceImportFields& fields, AssetPreview& preview);
bool import_vector(editor::ProjectSession& session,
                   SourceImportFields& fields, AssetPreview& preview);

void draw_import_workflow(editor::ProjectSession& session, SDL_Window* window,
                          ImportUiState& imports, AssetPreview& preview,
                          AssetPreview& pending_preview, bool& request_png,
                          bool& request_svg, std::string& status);

} // namespace fabric::asset_studio
