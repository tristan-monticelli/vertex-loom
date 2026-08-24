#include "fabric/editor/project_session.hpp"
#include "fabric/render/raster_image.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

constexpr ImGuiWindowFlags fixed_panel_flags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

struct ProjectCreationFields {
    std::array<char, 1024> path{};
    std::array<char, 129> id{};
    std::array<char, 256> name{};
    bool attempted{false};
};

struct TexturePreview {
    GLuint texture{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::array<char, 1024> path{};
    std::array<char, 129> id{};
    std::array<char, 256> name{};
    bool attempted{false};
};

bool import_texture(fabric::editor::ProjectSession& session,
                    TexturePreview& preview) {
    preview.attempted = true;
    if (!session.import_png(preview.path.data(), {.value = preview.id.data()},
                            preview.name.data())) {
        return false;
    }
    const auto& imported = *session.imported_texture();

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
                 static_cast<GLsizei>(imported.image.width),
                 static_cast<GLsizei>(imported.image.height), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, imported.image.rgba8.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    preview.width = imported.image.width;
    preview.height = imported.image.height;
    return true;
}

void clear_texture_preview(TexturePreview& preview) {
    if (preview.texture != 0U) {
        glDeleteTextures(1, &preview.texture);
        preview.texture = 0U;
    }
    preview.width = 0;
    preview.height = 0;
    preview.attempted = false;
}

void copy_path_to_buffer(const std::filesystem::path& path,
                         std::array<char, 1024>& buffer) {
    const std::string value = path.string();
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
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

void draw_project_tree(const fabric::editor::ProjectSession& session) {
    if (!session.has_project()) {
        ImGui::TextDisabled("No project open");
        ImGui::Spacing();
        ImGui::TextWrapped("Open a Vertex Loom project directory to inspect its content.");
        return;
    }

    const auto& directories = session.manifest()->directories;
    const std::array entries{
        std::pair{"Assets", &directories.assets},
        std::pair{"Entities", &directories.entities},
        std::pair{"Maps", &directories.maps},
        std::pair{"Scenes", &directories.scenes},
        std::pair{"Schemas", &directories.schemas},
    };
    for (const auto& [label, path] : entries) {
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Leaf)) {
            ImGui::TextDisabled("%s", path->generic_string().c_str());
            ImGui::TreePop();
        }
    }
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

void draw_workspace(fabric::editor::ProjectSession& session,
                    std::array<char, 1024>& path_buffer,
                    ProjectCreationFields& creation,
                    TexturePreview& preview,
                    bool& request_create,
                    bool& request_open,
                    bool& request_png,
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
    draw_project_tree(session);
    if (!session.has_project()) {
        ImGui::Spacing();
        if (ImGui::Button("Create project", {-1.0F, 0.0F})) {
            request_create = true;
        }
        if (ImGui::Button("Open project", {-1.0F, 0.0F})) {
            request_open = true;
        }
    } else if (ImGui::Button("Import PNG", {-1.0F, 0.0F})) {
        preview.attempted = false;
        request_png = true;
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
    if (preview.texture != 0U) {
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
                                          ? "Import a PNG to begin"
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
        ImGui::TextUnformatted(session.manifest()->name.c_str());
        ImGui::TextDisabled("%s", session.manifest()->id.value.c_str());
        ImGui::Separator();
        ImGui::Text("Schema version: %u", session.manifest()->schema_version);
        ImGui::TextWrapped("%s", session.project_root().string().c_str());
        if (preview.texture != 0U) {
            ImGui::SeparatorText("Imported texture");
            ImGui::Text("%u x %u RGBA8", preview.width, preview.height);
            if (session.imported_texture()) {
                ImGui::TextUnformatted(
                    session.imported_texture()->asset.document.name.c_str());
                ImGui::TextDisabled("%s",
                    session.imported_texture()->asset.document.id.value.c_str());
            }
            if (session.imported_texture()) {
                ImGui::TextWrapped("%s",
                    session.imported_texture()->asset.source.generic_string().c_str());
            }
        }
    } else {
        ImGui::TextDisabled("No selection");
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Diagnostics");
    draw_diagnostics(session);
    ImGui::End();

    ImGui::SetNextWindowPos({viewport->Pos.x,
                             viewport->Pos.y + viewport->Size.y - status_height});
    ImGui::SetNextWindowSize({viewport->Size.x, status_height});
    ImGui::Begin("Status", nullptr, fixed_panel_flags | ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted(status.c_str());
    ImGui::End();

    if (request_create) {
        creation.attempted = false;
        ImGui::OpenPopup("New project");
        request_create = false;
    }
    if (request_open) {
        ImGui::OpenPopup("Open project");
        request_open = false;
    }
    if (request_png) {
        preview.attempted = false;
        ImGui::OpenPopup("Import PNG");
        request_png = false;
    }

    if (ImGui::BeginPopupModal("Import PNG", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("PNG source file");
        ImGui::SetNextItemWidth(560.0F);
        const bool submitted = ImGui::InputText(
            "##png-path", preview.path.data(), preview.path.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", preview.name.data(), preview.name.size());
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Resource ID", preview.id.data(), preview.id.size());
        ImGui::TextDisabled("The PNG and its versioned document are copied into assets/textures.");
        if (submitted || ImGui::Button("Import", {110.0F, 0.0F})) {
            if (import_texture(session, preview)) {
                status = "PNG imported: " +
                    session.imported_texture()->asset.document.id.value;
                ImGui::CloseCurrentPopup();
            } else {
                status = "PNG import failed; inspect the diagnostics.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
        }
        if (preview.attempted && !session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Import failed");
            draw_diagnostics(session);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("New project", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a versioned Vertex Loom project");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Destination", creation.path.data(), creation.path.size());
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", creation.name.data(), creation.name.size());
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Resource ID", creation.id.data(), creation.id.size());
        ImGui::TextDisabled("Lowercase letters, digits, dots, underscores or hyphens.");
        ImGui::Spacing();
        if (ImGui::Button("Create", {110.0F, 0.0F})) {
            creation.attempted = true;
            const fabric::project::ProjectManifest manifest{
                .schema_version = fabric::project::current_schema_version,
                .id = {.value = creation.id.data()},
                .name = creation.name.data(),
                .directories = {},
            };
            if (session.create(creation.path.data(), manifest)) {
                clear_texture_preview(preview);
                status = "Project created: " + session.manifest()->name;
                copy_path_to_buffer(session.project_root(), path_buffer);
                ImGui::CloseCurrentPopup();
            } else {
                status = "Project creation failed; inspect the diagnostics.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
        }
        if (creation.attempted && !session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Creation failed");
            draw_diagnostics(session);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Open project", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Vertex Loom project directory");
        ImGui::SetNextItemWidth(560.0F);
        const bool submitted = ImGui::InputText("##project-path", path_buffer.data(),
                                                path_buffer.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        if (submitted || ImGui::Button("Open", {110.0F, 0.0F})) {
            if (path_buffer.front() == '\0') {
                status = "A project directory is required.";
            } else if (session.open(path_buffer.data())) {
                clear_texture_preview(preview);
                status = "Project opened: " + session.manifest()->name;
                ImGui::CloseCurrentPopup();
            } else {
                status = "Project rejected; inspect the diagnostics.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
        }
        if (!session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Validation failed");
            draw_diagnostics(session);
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

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        std::cerr << "OpenGL context creation failed: " << SDL_GetError() << '\n';
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
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    fabric::editor::ProjectSession session;
    std::array<char, 1024> path_buffer{};
    ProjectCreationFields creation;
    TexturePreview preview;
    bool request_create = false;
    bool request_open = false;
    bool request_png = false;
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
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New project...", "Ctrl+N")) {
                    request_create = true;
                }
                if (ImGui::MenuItem("Open project...", "Ctrl+O")) {
                    if (session.has_project()) {
                        copy_path_to_buffer(session.project_root(), path_buffer);
                    }
                    request_open = true;
                }
                if (ImGui::MenuItem("Import PNG...", "Ctrl+I", false,
                                    session.has_project())) {
                    request_png = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                    running = false;
                }
                ImGui::EndMenu();
            }
            ImGui::TextDisabled("Phase 2 - static authoring foundation");
            ImGui::EndMainMenuBar();
        }
        const auto& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            request_open = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            request_create = true;
        }
        if (io.KeyCtrl && session.has_project() &&
            ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            request_png = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            running = false;
        }

        draw_workspace(session, path_buffer, creation, preview, request_create,
                       request_open, request_png, status);

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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
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
