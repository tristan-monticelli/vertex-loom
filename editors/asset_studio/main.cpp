#include "fabric/editor/project_session.hpp"

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
    const char* preview_message = session.has_project()
                                      ? "Asset preview - import pipeline next"
                                      : "Open a project to begin";
    const ImVec2 text_size = ImGui::CalcTextSize(preview_message);
    draw_list->AddText({origin.x + (available.x - text_size.x) * 0.5F,
                        origin.y + (available.y - text_size.y) * 0.5F},
                       IM_COL32(158, 170, 180, 255), preview_message);
    ImGui::Dummy(available);
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
                if (ImGui::MenuItem("Open project...", "Ctrl+O")) {
                    if (session.has_project()) {
                        copy_path_to_buffer(session.project_root(), path_buffer);
                    }
                    ImGui::OpenPopup("Open project");
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
            ImGui::OpenPopup("Open project");
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            running = false;
        }

        draw_workspace(session, path_buffer, status);

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
