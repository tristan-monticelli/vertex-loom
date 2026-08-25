#include "fabric/render/opengl_vector_renderer.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fabric::render {
namespace {

using CreateShader = PFNGLCREATESHADERPROC;
using ShaderSource = PFNGLSHADERSOURCEPROC;
using CompileShader = PFNGLCOMPILESHADERPROC;
using GetShaderiv = PFNGLGETSHADERIVPROC;
using GetShaderInfoLog = PFNGLGETSHADERINFOLOGPROC;
using DeleteShader = PFNGLDELETESHADERPROC;
using CreateProgram = PFNGLCREATEPROGRAMPROC;
using AttachShader = PFNGLATTACHSHADERPROC;
using LinkProgram = PFNGLLINKPROGRAMPROC;
using GetProgramiv = PFNGLGETPROGRAMIVPROC;
using GetProgramInfoLog = PFNGLGETPROGRAMINFOLOGPROC;
using DeleteProgram = PFNGLDELETEPROGRAMPROC;
using UseProgram = PFNGLUSEPROGRAMPROC;
using GenVertexArrays = PFNGLGENVERTEXARRAYSPROC;
using BindVertexArray = PFNGLBINDVERTEXARRAYPROC;
using DeleteVertexArrays = PFNGLDELETEVERTEXARRAYSPROC;
using GenBuffers = PFNGLGENBUFFERSPROC;
using BindBuffer = PFNGLBINDBUFFERPROC;
using BufferData = PFNGLBUFFERDATAPROC;
using DeleteBuffers = PFNGLDELETEBUFFERSPROC;
using EnableVertexAttribArray = PFNGLENABLEVERTEXATTRIBARRAYPROC;
using VertexAttribPointer = PFNGLVERTEXATTRIBPOINTERPROC;
using DrawElements = void (GLAPIENTRY *)(GLenum, GLsizei, GLenum, const void*);
using GetUniformLocation = PFNGLGETUNIFORMLOCATIONPROC;
using UniformMatrix4fv = PFNGLUNIFORMMATRIX4FVPROC;
using Uniform4f = PFNGLUNIFORM4FPROC;

struct Functions {
    CreateShader create_shader{};
    ShaderSource shader_source{};
    CompileShader compile_shader{};
    GetShaderiv get_shader_iv{};
    GetShaderInfoLog get_shader_info_log{};
    DeleteShader delete_shader{};
    CreateProgram create_program{};
    AttachShader attach_shader{};
    LinkProgram link_program{};
    GetProgramiv get_program_iv{};
    GetProgramInfoLog get_program_info_log{};
    DeleteProgram delete_program{};
    UseProgram use_program{};
    GenVertexArrays gen_vertex_arrays{};
    BindVertexArray bind_vertex_array{};
    DeleteVertexArrays delete_vertex_arrays{};
    GenBuffers gen_buffers{};
    BindBuffer bind_buffer{};
    BufferData buffer_data{};
    DeleteBuffers delete_buffers{};
    EnableVertexAttribArray enable_vertex_attrib_array{};
    VertexAttribPointer vertex_attrib_pointer{};
    DrawElements draw_elements{};
    GetUniformLocation get_uniform_location{};
    UniformMatrix4fv uniform_matrix_4fv{};
    Uniform4f uniform_4f{};
};

template <typename Function>
Function load_function(const char* name) {
    return reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
}

Functions load_functions() {
    return {
        load_function<CreateShader>("glCreateShader"),
        load_function<ShaderSource>("glShaderSource"),
        load_function<CompileShader>("glCompileShader"),
        load_function<GetShaderiv>("glGetShaderiv"),
        load_function<GetShaderInfoLog>("glGetShaderInfoLog"),
        load_function<DeleteShader>("glDeleteShader"),
        load_function<CreateProgram>("glCreateProgram"),
        load_function<AttachShader>("glAttachShader"),
        load_function<LinkProgram>("glLinkProgram"),
        load_function<GetProgramiv>("glGetProgramiv"),
        load_function<GetProgramInfoLog>("glGetProgramInfoLog"),
        load_function<DeleteProgram>("glDeleteProgram"),
        load_function<UseProgram>("glUseProgram"),
        load_function<GenVertexArrays>("glGenVertexArrays"),
        load_function<BindVertexArray>("glBindVertexArray"),
        load_function<DeleteVertexArrays>("glDeleteVertexArrays"),
        load_function<GenBuffers>("glGenBuffers"),
        load_function<BindBuffer>("glBindBuffer"),
        load_function<BufferData>("glBufferData"),
        load_function<DeleteBuffers>("glDeleteBuffers"),
        load_function<EnableVertexAttribArray>("glEnableVertexAttribArray"),
        load_function<VertexAttribPointer>("glVertexAttribPointer"),
        load_function<DrawElements>("glDrawElements"),
        load_function<GetUniformLocation>("glGetUniformLocation"),
        load_function<UniformMatrix4fv>("glUniformMatrix4fv"),
        load_function<Uniform4f>("glUniform4f"),
    };
}

bool functions_ready(const Functions& functions) {
    return functions.create_shader != nullptr &&
           functions.shader_source != nullptr &&
           functions.compile_shader != nullptr &&
           functions.get_shader_iv != nullptr &&
           functions.get_shader_info_log != nullptr &&
           functions.delete_shader != nullptr &&
           functions.create_program != nullptr &&
           functions.attach_shader != nullptr &&
           functions.link_program != nullptr &&
           functions.get_program_iv != nullptr &&
           functions.get_program_info_log != nullptr &&
           functions.delete_program != nullptr &&
           functions.use_program != nullptr &&
           functions.gen_vertex_arrays != nullptr &&
           functions.bind_vertex_array != nullptr &&
           functions.delete_vertex_arrays != nullptr &&
           functions.gen_buffers != nullptr &&
           functions.bind_buffer != nullptr &&
           functions.buffer_data != nullptr &&
           functions.delete_buffers != nullptr &&
           functions.enable_vertex_attrib_array != nullptr &&
           functions.vertex_attrib_pointer != nullptr &&
           functions.draw_elements != nullptr &&
           functions.get_uniform_location != nullptr &&
           functions.uniform_matrix_4fv != nullptr &&
           functions.uniform_4f != nullptr;
}

std::string shader_log(const Functions& functions, const GLuint shader) {
    GLint length = 0;
    functions.get_shader_iv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    functions.get_shader_info_log(shader, length, nullptr, log.data());
    return log;
}

GLuint compile_shader(const Functions& functions, const GLenum type,
                      const char* source, std::string& error) {
    const GLuint shader = functions.create_shader(type);
    functions.shader_source(shader, 1, &source, nullptr);
    functions.compile_shader(shader);
    GLint compiled = GL_FALSE;
    functions.get_shader_iv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        error = shader_log(functions, shader);
        functions.delete_shader(shader);
        return 0U;
    }
    return shader;
}

std::array<float, 16> world_to_clip(const OpenGLVectorViewport& viewport) {
    const float width = viewport.world_bounds.size.x;
    const float height = viewport.world_bounds.size.y;
    const float left = viewport.world_bounds.origin.x;
    const float bottom = viewport.world_bounds.origin.y;
    return {2.0F / width, 0.0F, 0.0F, 0.0F,
            0.0F, 2.0F / height, 0.0F, 0.0F,
            0.0F, 0.0F, -1.0F, 0.0F,
            -(2.0F * left + width) / width,
            -(2.0F * bottom + height) / height, 0.0F, 1.0F};
}

struct Vertex {
    float x;
    float y;
};

} // namespace

OpenGLVectorRenderer::~OpenGLVectorRenderer() { shutdown(); }

bool OpenGLVectorRenderer::initialize() {
    if (ready()) return true;
    const auto functions = load_functions();
    if (!functions_ready(functions)) return false;

    constexpr const char* vertex_source = R"GLSL(#version 130
in vec2 position;
uniform mat4 worldToClip;
void main() { gl_Position = worldToClip * vec4(position, 0.0, 1.0); }
)GLSL";
    constexpr const char* fragment_source = R"GLSL(#version 130
uniform vec4 color;
out vec4 fragmentColor;
void main() { fragmentColor = color; }
)GLSL";
    std::string error;
    const GLuint vertex_shader = compile_shader(
        functions, GL_VERTEX_SHADER, vertex_source, error);
    if (vertex_shader == 0U) return false;
    const GLuint fragment_shader = compile_shader(
        functions, GL_FRAGMENT_SHADER, fragment_source, error);
    if (fragment_shader == 0U) {
        functions.delete_shader(vertex_shader);
        return false;
    }
    const GLuint program = functions.create_program();
    functions.attach_shader(program, vertex_shader);
    functions.attach_shader(program, fragment_shader);
    functions.link_program(program);
    functions.delete_shader(vertex_shader);
    functions.delete_shader(fragment_shader);
    GLint linked = GL_FALSE;
    functions.get_program_iv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        functions.delete_program(program);
        return false;
    }
    functions.gen_vertex_arrays(1, &vertex_array_);
    functions.gen_buffers(1, &vertex_buffer_);
    functions.gen_buffers(1, &index_buffer_);
    functions.bind_vertex_array(vertex_array_);
    functions.bind_buffer(GL_ARRAY_BUFFER, vertex_buffer_);
    const GLint position = 0;
    functions.enable_vertex_attrib_array(position);
    functions.vertex_attrib_pointer(position, 2, GL_FLOAT, GL_FALSE,
                                     sizeof(Vertex), nullptr);
    functions.bind_vertex_array(0U);
    program_ = program;
    world_to_clip_uniform_ = functions.get_uniform_location(
        program_, "worldToClip");
    if (world_to_clip_uniform_ < 0) {
        functions.delete_buffers(1, &vertex_buffer_);
        functions.delete_buffers(1, &index_buffer_);
        functions.delete_vertex_arrays(1, &vertex_array_);
        functions.delete_program(program_);
        program_ = 0U;
        return false;
    }
    return true;
}

void OpenGLVectorRenderer::shutdown() noexcept {
    if (!ready()) return;
    const auto functions = load_functions();
    if (functions.delete_buffers != nullptr) {
        functions.delete_buffers(1, &vertex_buffer_);
        functions.delete_buffers(1, &index_buffer_);
        functions.delete_vertex_arrays(1, &vertex_array_);
        functions.delete_program(program_);
    }
    program_ = 0U;
    vertex_array_ = 0U;
    vertex_buffer_ = 0U;
    index_buffer_ = 0U;
    world_to_clip_uniform_ = -1;
}

OpenGLVectorRenderStats OpenGLVectorRenderer::draw(
    const std::span<const VectorDrawPacket> packets,
    const OpenGLVectorViewport& viewport) {
    OpenGLVectorRenderStats stats;
    stats.packets_submitted = static_cast<std::uint32_t>(packets.size());
    if (!ready()) {
        stats.errors.push_back("OpenGL vector renderer is not initialized");
        return stats;
    }
    if (viewport.width <= 0 || viewport.height <= 0 ||
        viewport.x < 0 || viewport.y < 0 ||
        !std::isfinite(viewport.world_bounds.size.x) ||
        !std::isfinite(viewport.world_bounds.size.y) ||
        viewport.world_bounds.size.x <= 0.0F ||
        viewport.world_bounds.size.y <= 0.0F) {
        stats.errors.push_back("OpenGL vector viewport must be finite and positive");
        return stats;
    }
    const auto functions = load_functions();
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    functions.use_program(program_);
    const auto matrix = world_to_clip(viewport);
    functions.uniform_matrix_4fv(world_to_clip_uniform_, 1, GL_FALSE,
                                 matrix.data());
    functions.bind_vertex_array(vertex_array_);
    for (const auto& packet : packets) {
        if (packet.image_fill.has_value()) {
            stats.errors.push_back(
                "OpenGL vector renderer requires a texture resolver for image fills");
            continue;
        }
        if (!packet.fill_color.has_value() || packet.fill_indices.empty()) {
            continue;
        }
        std::vector<Vertex> vertices;
        vertices.reserve(packet.fill_vertices.size());
        for (const auto point : packet.fill_vertices) {
            vertices.push_back({point.x, point.y});
        }
        functions.bind_buffer(GL_ARRAY_BUFFER, vertex_buffer_);
        functions.buffer_data(GL_ARRAY_BUFFER,
                              static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                              vertices.data(), GL_STREAM_DRAW);
        functions.bind_buffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
        functions.buffer_data(GL_ELEMENT_ARRAY_BUFFER,
                              static_cast<GLsizeiptr>(packet.fill_indices.size() *
                                                      sizeof(std::uint32_t)),
                              packet.fill_indices.data(), GL_STREAM_DRAW);
        const auto& color = *packet.fill_color;
        const GLint color_uniform = functions.get_uniform_location(program_, "color");
        functions.uniform_4f(color_uniform, color.red, color.green, color.blue,
                             color.alpha);
        functions.draw_elements(GL_TRIANGLES,
                                static_cast<GLsizei>(packet.fill_indices.size()),
                                GL_UNSIGNED_INT, nullptr);
        ++stats.packets_drawn;
        stats.triangles_drawn += static_cast<std::uint32_t>(
            packet.fill_indices.size() / 3U);
    }
    functions.bind_vertex_array(0U);
    functions.use_program(0U);
    return stats;
}

} // namespace fabric::render
