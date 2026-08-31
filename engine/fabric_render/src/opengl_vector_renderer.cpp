#include "fabric/render/opengl_vector_renderer.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
using BindAttribLocation = PFNGLBINDATTRIBLOCATIONPROC;
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
using BufferSubData = PFNGLBUFFERSUBDATAPROC;
using DeleteBuffers = PFNGLDELETEBUFFERSPROC;
using EnableVertexAttribArray = PFNGLENABLEVERTEXATTRIBARRAYPROC;
using VertexAttribPointer = PFNGLVERTEXATTRIBPOINTERPROC;
using DrawElements = void (GLAPIENTRY *)(GLenum, GLsizei, GLenum, const void*);
using DrawArrays = void (GLAPIENTRY *)(GLenum, GLint, GLsizei);
using GetUniformLocation = PFNGLGETUNIFORMLOCATIONPROC;
using UniformMatrix4fv = PFNGLUNIFORMMATRIX4FVPROC;
using Uniform4f = PFNGLUNIFORM4FPROC;
using Uniform1i = PFNGLUNIFORM1IPROC;
using Uniform1f = PFNGLUNIFORM1FPROC;
using ActiveTexture = PFNGLACTIVETEXTUREPROC;
using BindTexture = void (GLAPIENTRY *)(GLenum, GLuint);

struct Functions {
    CreateShader create_shader{};
    ShaderSource shader_source{};
    CompileShader compile_shader{};
    GetShaderiv get_shader_iv{};
    GetShaderInfoLog get_shader_info_log{};
    DeleteShader delete_shader{};
    CreateProgram create_program{};
    AttachShader attach_shader{};
    BindAttribLocation bind_attrib_location{};
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
    BufferSubData buffer_sub_data{};
    DeleteBuffers delete_buffers{};
    EnableVertexAttribArray enable_vertex_attrib_array{};
    VertexAttribPointer vertex_attrib_pointer{};
    DrawElements draw_elements{};
    DrawArrays draw_arrays{};
    GetUniformLocation get_uniform_location{};
    UniformMatrix4fv uniform_matrix_4fv{};
    Uniform4f uniform_4f{};
    Uniform1i uniform_1i{};
    Uniform1f uniform_1f{};
    ActiveTexture active_texture{};
    BindTexture bind_texture{};
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
        load_function<BindAttribLocation>("glBindAttribLocation"),
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
        load_function<BufferSubData>("glBufferSubData"),
        load_function<DeleteBuffers>("glDeleteBuffers"),
        load_function<EnableVertexAttribArray>("glEnableVertexAttribArray"),
        load_function<VertexAttribPointer>("glVertexAttribPointer"),
        load_function<DrawElements>("glDrawElements"),
        load_function<DrawArrays>("glDrawArrays"),
        load_function<GetUniformLocation>("glGetUniformLocation"),
        load_function<UniformMatrix4fv>("glUniformMatrix4fv"),
        load_function<Uniform4f>("glUniform4f"),
        load_function<Uniform1i>("glUniform1i"),
        load_function<Uniform1f>("glUniform1f"),
        load_function<ActiveTexture>("glActiveTexture"),
        load_function<BindTexture>("glBindTexture"),
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
           functions.bind_attrib_location != nullptr &&
           functions.link_program != nullptr &&
           functions.get_program_iv != nullptr &&
           functions.get_program_info_log != nullptr &&
           functions.delete_program != nullptr &&
           functions.use_program != nullptr &&
           functions.gen_buffers != nullptr &&
           functions.bind_buffer != nullptr &&
           functions.buffer_data != nullptr &&
           functions.buffer_sub_data != nullptr &&
           functions.delete_buffers != nullptr &&
           functions.enable_vertex_attrib_array != nullptr &&
           functions.vertex_attrib_pointer != nullptr &&
           functions.draw_elements != nullptr &&
           functions.draw_arrays != nullptr &&
           functions.get_uniform_location != nullptr &&
           functions.uniform_matrix_4fv != nullptr &&
           functions.uniform_4f != nullptr &&
           functions.uniform_1i != nullptr &&
           functions.uniform_1f != nullptr &&
           functions.active_texture != nullptr &&
           functions.bind_texture != nullptr;
}

std::string shader_log(const Functions& functions, const GLuint shader) {
    GLint length = 0;
    functions.get_shader_iv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    functions.get_shader_info_log(shader, length, nullptr, log.data());
    return log;
}

std::string program_log(const Functions& functions, const GLuint program) {
    GLint length = 0;
    functions.get_program_iv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    functions.get_program_info_log(program, length, nullptr, log.data());
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

core::Color texture_tint(const VectorDrawPacket& packet) {
    return packet.fill_color.value_or(core::Color{});
}

bool same_texture_tint(const VectorDrawPacket& left,
                       const VectorDrawPacket& right) {
    return texture_tint(left) == texture_tint(right);
}

struct ClipVertex {
    core::Vec2 position;
    core::Vec2 uv;
};

float polygon_area(const std::vector<core::Vec2>& polygon) {
    float area = 0.0F;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const auto& current = polygon[index];
        const auto& next = polygon[(index + 1U) % polygon.size()];
        area += current.x * next.y - next.x * current.y;
    }
    return area * 0.5F;
}

bool is_convex_polygon(const std::vector<core::Vec2>& polygon) {
    if (polygon.size() < 3U) return false;
    int sign = 0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const auto& first = polygon[index];
        const auto& second = polygon[(index + 1U) % polygon.size()];
        const auto& third = polygon[(index + 2U) % polygon.size()];
        const float turn = (second.x - first.x) * (third.y - second.y) -
            (second.y - first.y) * (third.x - second.x);
        if (std::abs(turn) <= 1.0e-6F) continue;
        const int next_sign = turn > 0.0F ? 1 : -1;
        if (sign != 0 && sign != next_sign) return false;
        sign = next_sign;
    }
    return sign != 0;
}

std::optional<std::vector<ClipVertex>> clip_convex_polygon(
    std::vector<ClipVertex> subject, const std::vector<core::Vec2>& clip) {
    if (!is_convex_polygon(clip) || subject.size() < 3U) return std::nullopt;
    const float orientation = polygon_area(clip) >= 0.0F ? 1.0F : -1.0F;
    for (std::size_t edge = 0; edge < clip.size(); ++edge) {
        const auto edge_start = clip[edge];
        const auto edge_end = clip[(edge + 1U) % clip.size()];
        const auto signed_distance = [&](const core::Vec2 point) {
            return orientation * ((edge_end.x - edge_start.x) *
                                      (point.y - edge_start.y) -
                                  (edge_end.y - edge_start.y) *
                                      (point.x - edge_start.x));
        };
        std::vector<ClipVertex> output;
        for (std::size_t index = 0; index < subject.size(); ++index) {
            const auto& first = subject[index];
            const auto& second = subject[(index + 1U) % subject.size()];
            const float first_distance = signed_distance(first.position);
            const float second_distance = signed_distance(second.position);
            const bool first_inside = first_distance >= -1.0e-5F;
            const bool second_inside = second_distance >= -1.0e-5F;
            if (first_inside != second_inside) {
                const float denominator = first_distance - second_distance;
                const float factor = std::abs(denominator) > 1.0e-6F
                    ? first_distance / denominator : 0.0F;
                output.push_back({
                    .position = {first.position.x +
                                     (second.position.x - first.position.x) * factor,
                                 first.position.y +
                                     (second.position.y - first.position.y) * factor},
                    .uv = {first.uv.x + (second.uv.x - first.uv.x) * factor,
                           first.uv.y + (second.uv.y - first.uv.y) * factor}});
            }
            if (second_inside) output.push_back(second);
        }
        subject = std::move(output);
        if (subject.size() < 3U) return subject;
    }
    return subject;
}

std::optional<VectorDrawPacket> cpu_clip_packet(
    const VectorDrawPacket& packet,
    const std::vector<const VectorDrawPacket*>& clip_chain) {
    if (packet.fill_vertices.size() < 3U || packet.fill_indices.size() < 3U ||
        packet.fill_indices.size() % 3U != 0U)
        return std::nullopt;
    for (const auto* clip : clip_chain)
        if (clip == nullptr || !is_convex_polygon(clip->outline))
            return std::nullopt;
    VectorDrawPacket result = packet;
    result.clip_node_id.reset();
    result.outline.clear();
    result.fill_vertices.clear();
    result.fill_uv.clear();
    result.fill_indices.clear();
    const auto clip_mesh = [&](const std::vector<core::Vec2>& vertices,
                               const std::vector<core::Vec2>& uvs,
                               const std::vector<std::uint32_t>& indices,
                               std::vector<core::Vec2>& output_vertices,
                               std::vector<core::Vec2>& output_uvs,
                               std::vector<std::uint32_t>& output_indices) {
        if (indices.size() < 3U || indices.size() % 3U != 0U) return false;
        for (std::size_t index = 0; index < indices.size(); index += 3U) {
            if (indices[index] >= vertices.size() ||
                indices[index + 1U] >= vertices.size() ||
                indices[index + 2U] >= vertices.size()) return false;
            std::vector<ClipVertex> subject;
            subject.reserve(3U);
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                const auto vertex_index = indices[index + corner];
                subject.push_back({vertices[vertex_index],
                                   uvs.size() == vertices.size()
                                       ? uvs[vertex_index] : core::Vec2{}});
            }
            for (const auto* clip : clip_chain) {
                const auto clipped = clip_convex_polygon(std::move(subject),
                                                          clip->outline);
                if (!clipped) return false;
                subject = *clipped;
                if (subject.size() < 3U) break;
            }
            for (std::uint32_t corner = 1U;
                 corner + 1U < subject.size(); ++corner) {
                const auto base = static_cast<std::uint32_t>(output_vertices.size());
                output_vertices.push_back(subject[0].position);
                output_vertices.push_back(subject[corner].position);
                output_vertices.push_back(subject[corner + 1U].position);
                output_uvs.push_back(subject[0].uv);
                output_uvs.push_back(subject[corner].uv);
                output_uvs.push_back(subject[corner + 1U].uv);
                output_indices.insert(output_indices.end(), {base, base + 1U,
                                                              base + 2U});
            }
        }
        return true;
    };
    if (!clip_mesh(packet.fill_vertices, packet.fill_uv, packet.fill_indices,
                   result.fill_vertices, result.fill_uv, result.fill_indices))
        return std::nullopt;
    if (result.fill_indices.empty()) return std::nullopt;
    result.outline = result.fill_vertices;
    if (packet.stroke && !packet.stroke_vertices.empty() &&
        !packet.stroke_indices.empty()) {
        if (!clip_mesh(packet.stroke_vertices, packet.stroke_uv,
                       packet.stroke_indices, result.stroke_vertices,
                       result.stroke_uv, result.stroke_indices))
            return std::nullopt;
    }
    result.closed_outline = true;
    return result;
}

} // namespace

OpenGLVectorRenderer::~OpenGLVectorRenderer() { shutdown(); }

bool OpenGLVectorRenderer::initialize() {
    if (ready()) return true;
    initialization_error_.clear();
    int context_major = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &context_major);
    const auto functions = load_functions();
    if (!functions_ready(functions)) {
        if (context_major <= 2) {
            legacy_fixed_function_ = true;
            return true;
        }
        initialization_error_ = "required OpenGL entry point is unavailable";
        return false;
    }

    use_vertex_array_ = context_major >= 3 &&
        functions.gen_vertex_arrays != nullptr &&
        functions.bind_vertex_array != nullptr &&
        functions.delete_vertex_arrays != nullptr;

    const std::string vertex_source = context_major < 3
        ? std::string{R"GLSL(#version 120
attribute vec2 position;
attribute vec2 uv;
varying vec2 fragmentUv;
uniform mat4 worldToClip;
void main() {
    fragmentUv = uv;
    gl_Position = worldToClip * vec4(position, 0.0, 1.0);
}
)GLSL"}
        : std::string{
#if defined(__APPLE__)
            "#version 150\n"
#else
            "#version 130\n"
#endif
R"GLSL(
in vec2 position;
in vec2 uv;
out vec2 fragmentUv;
uniform mat4 worldToClip;
void main() {
    fragmentUv = uv;
    gl_Position = worldToClip * vec4(position, 0.0, 1.0);
}
)GLSL"};
    const std::string fragment_source = context_major < 3
        ? std::string{R"GLSL(#version 120
uniform vec4 color;
uniform sampler2D imageTexture;
uniform int textured;
uniform float opacity;
uniform int shaderEnabled;
uniform vec4 shaderPrimary;
uniform vec4 shaderEffect;
uniform float shaderShine;
uniform float shaderHolography;
uniform float shaderOpacity;
uniform float shaderIntensity;
varying vec2 fragmentUv;
void main() {
    vec4 base = textured == 1
        ? texture2D(imageTexture, fragmentUv) * color * opacity
        : color;
    if (shaderEnabled == 1) base = vec4(mix(base.rgb * shaderPrimary.rgb,
        shaderEffect.rgb, clamp(shaderHolography, 0.0, 1.0)) * shaderIntensity
        + vec3(shaderShine), mix(base.a * shaderPrimary.a, shaderEffect.a,
        clamp(shaderHolography, 0.0, 1.0)) * shaderOpacity);
    gl_FragColor = base;
}
)GLSL"}
        : std::string{
#if defined(__APPLE__)
            "#version 150\n"
#else
            "#version 130\n"
#endif
R"GLSL(
uniform vec4 color;
uniform sampler2D imageTexture;
uniform int textured;
uniform float opacity;
uniform int shaderEnabled;
uniform vec4 shaderPrimary;
uniform vec4 shaderEffect;
uniform float shaderShine;
uniform float shaderHolography;
uniform float shaderOpacity;
uniform float shaderIntensity;
in vec2 fragmentUv;
out vec4 fragmentColor;
void main() {
    vec4 base = textured == 1
        ? texture(imageTexture, fragmentUv) * color * opacity
        : color;
    if (shaderEnabled == 1) base = vec4(mix(base.rgb * shaderPrimary.rgb,
        shaderEffect.rgb, clamp(shaderHolography, 0.0, 1.0)) * shaderIntensity
        + vec3(shaderShine), mix(base.a * shaderPrimary.a, shaderEffect.a,
        clamp(shaderHolography, 0.0, 1.0)) * shaderOpacity);
    fragmentColor = base;
}
)GLSL"};
    std::string error;
    const GLuint vertex_shader = compile_shader(
        functions, GL_VERTEX_SHADER, vertex_source.c_str(), error);
    if (vertex_shader == 0U) {
        initialization_error_ = "vertex shader: " + error;
        return false;
    }
    const GLuint fragment_shader = compile_shader(
        functions, GL_FRAGMENT_SHADER, fragment_source.c_str(), error);
    if (fragment_shader == 0U) {
        functions.delete_shader(vertex_shader);
        initialization_error_ = "fragment shader: " + error;
        return false;
    }
    const GLuint program = functions.create_program();
    functions.attach_shader(program, vertex_shader);
    functions.attach_shader(program, fragment_shader);
    functions.bind_attrib_location(program, 0, "position");
    functions.bind_attrib_location(program, 1, "uv");
    functions.link_program(program);
    functions.delete_shader(vertex_shader);
    functions.delete_shader(fragment_shader);
    GLint linked = GL_FALSE;
    functions.get_program_iv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        initialization_error_ = "program link: " + program_log(functions, program);
        functions.delete_program(program);
        return false;
    }
    if (use_vertex_array_) functions.gen_vertex_arrays(1, &vertex_array_);
    functions.gen_buffers(1, &vertex_buffer_);
    functions.gen_buffers(1, &index_buffer_);
    if (use_vertex_array_) functions.bind_vertex_array(vertex_array_);
    functions.bind_buffer(GL_ARRAY_BUFFER, vertex_buffer_);
    const GLint position = 0;
    functions.enable_vertex_attrib_array(position);
    functions.vertex_attrib_pointer(position, 2, GL_FLOAT, GL_FALSE,
                                     sizeof(Vertex), nullptr);
    const GLint uv = 1;
    functions.enable_vertex_attrib_array(uv);
    functions.vertex_attrib_pointer(uv, 2, GL_FLOAT, GL_FALSE,
                                     sizeof(Vertex),
                                     reinterpret_cast<const void*>(
                                         2U * sizeof(float)));
    if (use_vertex_array_) functions.bind_vertex_array(0U);
    program_ = program;
    world_to_clip_uniform_ = functions.get_uniform_location(
        program_, "worldToClip");
    color_uniform_ = functions.get_uniform_location(program_, "color");
    image_texture_uniform_ = functions.get_uniform_location(program_, "imageTexture");
    textured_uniform_ = functions.get_uniform_location(program_, "textured");
    opacity_uniform_ = functions.get_uniform_location(program_, "opacity");
    shader_enabled_uniform_ = functions.get_uniform_location(program_, "shaderEnabled");
    shader_primary_uniform_ = functions.get_uniform_location(program_, "shaderPrimary");
    shader_effect_uniform_ = functions.get_uniform_location(program_, "shaderEffect");
    shader_shine_uniform_ = functions.get_uniform_location(program_, "shaderShine");
    shader_holography_uniform_ = functions.get_uniform_location(program_, "shaderHolography");
    shader_opacity_uniform_ = functions.get_uniform_location(program_, "shaderOpacity");
    shader_intensity_uniform_ = functions.get_uniform_location(program_, "shaderIntensity");
    if (world_to_clip_uniform_ < 0 || color_uniform_ < 0 ||
        image_texture_uniform_ < 0 || textured_uniform_ < 0 || opacity_uniform_ < 0) {
        functions.delete_buffers(1, &vertex_buffer_);
        functions.delete_buffers(1, &index_buffer_);
        if (use_vertex_array_) functions.delete_vertex_arrays(1, &vertex_array_);
        if (program_ != 0U) functions.delete_program(program_);
        initialization_error_ = "required shader uniform is unavailable";
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
        if (use_vertex_array_) functions.delete_vertex_arrays(1, &vertex_array_);
        functions.delete_program(program_);
    }
    program_ = 0U;
    legacy_fixed_function_ = false;
    vertex_array_ = 0U;
    use_vertex_array_ = false;
    vertex_buffer_ = 0U;
    index_buffer_ = 0U;
    world_to_clip_uniform_ = -1;
    color_uniform_ = -1;
    image_texture_uniform_ = -1;
    textured_uniform_ = -1;
    opacity_uniform_ = -1;
    shader_enabled_uniform_ = -1;
    shader_primary_uniform_ = -1;
    shader_effect_uniform_ = -1;
    shader_shine_uniform_ = -1;
    shader_holography_uniform_ = -1;
    shader_opacity_uniform_ = -1;
    shader_intensity_uniform_ = -1;
    initialization_error_.clear();
    vertex_buffer_capacity_ = 0U;
    index_buffer_capacity_ = 0U;
    vertex_scratch_.clear();
    index_scratch_.clear();
    batch_scratch_.clear();
}

OpenGLVectorRenderStats OpenGLVectorRenderer::draw(
    const std::span<const VectorDrawPacket> packets,
    const OpenGLVectorViewport& viewport,
    const OpenGLTextureResolver& texture_resolver) {
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
    if (legacy_fixed_function_) {
        glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(viewport.world_bounds.origin.x,
                viewport.world_bounds.origin.x + viewport.world_bounds.size.x,
                viewport.world_bounds.origin.y,
                viewport.world_bounds.origin.y + viewport.world_bounds.size.y,
                -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glEnableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const auto same_material = [](const VectorDrawPacket& left,
                                      const VectorDrawPacket& right) {
            if (left.image_fill.has_value() != right.image_fill.has_value()) return false;
            if (left.image_fill) {
                return right.image_fill &&
                    left.image_fill->texture.id == right.image_fill->texture.id &&
                    left.image_fill->opacity == right.image_fill->opacity &&
                    left.raster_filter == right.raster_filter &&
                    left.repeat_texture_x == right.repeat_texture_x &&
                    same_texture_tint(left, right);
            }
            if (!left.fill_color || !right.fill_color) return false;
            return left.fill_color->red == right.fill_color->red &&
                left.fill_color->green == right.fill_color->green &&
                left.fill_color->blue == right.fill_color->blue &&
                left.fill_color->alpha == right.fill_color->alpha;
        };
        for (std::size_t packet_index = 0; packet_index < packets.size();) {
            const auto& packet = packets[packet_index];
            if (packet.stroke || (!packet.fill_color && !packet.image_fill) ||
                packet.fill_vertices.empty() ||
                packet.fill_indices.empty()) {
                ++packet_index;
                continue;
            }
            std::optional<OpenGLTextureHandle> texture;
            if (packet.image_fill) {
                if (!texture_resolver) {
                    stats.errors.push_back(
                        "legacy OpenGL renderer requires a texture resolver for image fills");
                    ++packet_index;
                    continue;
                }
                texture = texture_resolver(packet.image_fill->texture.id);
                if (!texture || texture->handle == 0U ||
                    packet.fill_uv.size() != packet.fill_vertices.size()) {
                    stats.errors.push_back(
                        "legacy OpenGL texture fill could not be resolved");
                    ++packet_index;
                    continue;
                }
            }
            vertex_scratch_.clear();
            index_scratch_.clear();
            const float texture_aspect = texture && texture->height > 0U
                ? static_cast<float>(texture->width) /
                      static_cast<float>(texture->height)
                : 1.0F;
            std::size_t next = packet_index;
            while (next < packets.size()) {
                const auto& candidate = packets[next];
                if (candidate.stroke ||
                    (!candidate.fill_color && !candidate.image_fill) ||
                    candidate.fill_vertices.empty() || candidate.fill_indices.empty() ||
                    !same_material(packet, candidate)) break;
                if (candidate.image_fill &&
                    candidate.fill_uv.size() != candidate.fill_vertices.size()) break;
                const auto base = static_cast<std::uint32_t>(vertex_scratch_.size());
                vertex_scratch_.reserve(vertex_scratch_.size() +
                                        candidate.fill_vertices.size());
                auto minimum = candidate.outline.empty()
                    ? core::Vec2{} : candidate.outline.front();
                auto maximum = minimum;
                for (const auto point : candidate.outline) {
                    minimum.x = std::min(minimum.x, point.x);
                    minimum.y = std::min(minimum.y, point.y);
                    maximum.x = std::max(maximum.x, point.x);
                    maximum.y = std::max(maximum.y, point.y);
                }
                const float geometry_aspect = maximum.y - minimum.y > 0.0001F
                    ? (maximum.x - minimum.x) / (maximum.y - minimum.y) : 1.0F;
                for (std::size_t index = 0; index < candidate.fill_vertices.size(); ++index) {
                    const auto point = candidate.fill_vertices[index];
                    const auto uv = candidate.image_fill
                        ? apply_image_fill_fit(candidate.fill_uv[index],
                                               candidate.image_fill->fit,
                                               geometry_aspect, texture_aspect)
                        : core::Vec2{};
                    vertex_scratch_.push_back({point.x, point.y, uv.x, uv.y});
                }
                index_scratch_.reserve(index_scratch_.size() +
                                       candidate.fill_indices.size());
                for (const auto index : candidate.fill_indices)
                    index_scratch_.push_back(base + index);
                ++next;
            }
            if (packet.image_fill) {
                glEnable(GL_TEXTURE_2D);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glBindTexture(GL_TEXTURE_2D, texture->handle);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                packet.repeat_texture_x
                                    ? GL_REPEAT : GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                GL_CLAMP_TO_EDGE);
                if (packet.raster_filter) {
                    const auto filter = *packet.raster_filter ==
                            project::RasterFilter::nearest
                        ? GL_NEAREST : GL_LINEAR;
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
                }
                const auto tint = texture_tint(packet);
                glColor4f(tint.red, tint.green, tint.blue,
                          tint.alpha * packet.image_fill->opacity);
                glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex),
                                  reinterpret_cast<const std::byte*>(
                                      vertex_scratch_.data()) +
                                      2U * sizeof(float));
            } else {
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                const auto& color = *packet.fill_color;
                glColor4f(color.red, color.green, color.blue, color.alpha);
            }
            glVertexPointer(2, GL_FLOAT, sizeof(Vertex), vertex_scratch_.data());
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(index_scratch_.size()),
                           GL_UNSIGNED_INT, index_scratch_.data());
            ++stats.draw_calls;
            stats.packets_drawn += static_cast<std::uint32_t>(next - packet_index);
            stats.triangles_drawn += static_cast<std::uint32_t>(
                index_scratch_.size() / 3U);
            packet_index = next;
        }
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        for (const auto& packet : packets) {
            if (!packet.stroke || packet.stroke_vertices.empty() ||
                packet.stroke_indices.empty()) continue;
            std::optional<OpenGLTextureHandle> texture;
            const bool textured = packet.stroke_image.has_value();
            if (textured) {
                if (!texture_resolver) {
                    stats.errors.push_back(
                        "legacy OpenGL renderer requires a texture resolver for strokes");
                    continue;
                }
                texture = texture_resolver(packet.stroke_image->texture.id);
                if (!texture || texture->handle == 0U ||
                    packet.stroke_uv.size() != packet.stroke_vertices.size()) {
                    stats.errors.push_back(
                        "legacy OpenGL textured stroke could not be resolved");
                    continue;
                }
            }
            vertex_scratch_.clear();
            vertex_scratch_.reserve(packet.stroke_vertices.size());
            for (std::size_t index = 0; index < packet.stroke_vertices.size(); ++index) {
                const auto point = packet.stroke_vertices[index];
                const auto uv = textured ? packet.stroke_uv[index] : core::Vec2{};
                vertex_scratch_.push_back({point.x, point.y, uv.x, uv.y});
            }
            if (textured) {
                glEnable(GL_TEXTURE_2D);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glBindTexture(GL_TEXTURE_2D, texture->handle);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                packet.stroke_repeat_texture_x
                                    ? GL_REPEAT : GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glColor4f(packet.stroke->color.red, packet.stroke->color.green,
                          packet.stroke->color.blue,
                          packet.stroke->color.alpha * packet.stroke_image->opacity);
                glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex),
                                  reinterpret_cast<const std::byte*>(
                                      vertex_scratch_.data()) + 2U * sizeof(float));
            } else {
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                glColor4f(packet.stroke->color.red, packet.stroke->color.green,
                          packet.stroke->color.blue, packet.stroke->color.alpha);
            }
            glVertexPointer(2, GL_FLOAT, sizeof(Vertex), vertex_scratch_.data());
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(packet.stroke_indices.size()),
                           GL_UNSIGNED_INT, packet.stroke_indices.data());
            ++stats.draw_calls;
            ++stats.packets_drawn;
            stats.triangles_drawn += static_cast<std::uint32_t>(
                packet.stroke_indices.size() / 3U);
        }
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_BLEND);
        glDisableClientState(GL_VERTEX_ARRAY);
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        return stats;
    }
    const auto functions = load_functions();
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    functions.use_program(program_);
    const auto matrix = world_to_clip(viewport);
    functions.uniform_matrix_4fv(world_to_clip_uniform_, 1, GL_FALSE,
                                 matrix.data());
    if (use_vertex_array_) functions.bind_vertex_array(vertex_array_);

    const auto upload_buffer = [&](const GLenum target, const std::size_t size,
                                   const void* data, std::size_t& capacity) {
        functions.bind_buffer(target, target == GL_ARRAY_BUFFER
            ? vertex_buffer_ : index_buffer_);
        if (size > capacity) {
            functions.buffer_data(target, static_cast<GLsizeiptr>(size),
                                  nullptr, GL_STREAM_DRAW);
            capacity = size;
        }
        functions.buffer_sub_data(target, 0, static_cast<GLsizeiptr>(size), data);
    };

    std::unordered_map<std::string, const VectorDrawPacket*> packets_by_id;
    const bool has_clips = std::ranges::any_of(packets,
        [](const auto& packet) { return packet.clip_node_id.has_value(); });
    if (has_clips)
        for (const auto& packet : packets)
            packets_by_id.emplace(packet.node_id, &packet);
    bool stencil_ready = false;
    if (has_clips) {
        GLint stencil_bits = 0;
        glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
        if (stencil_bits <= 0) {
            // Convex clip chains use the CPU fallback below when the context
            // has no stencil attachment. Unsupported silhouettes still report
            // an error when the fallback is attempted.
        } else {
            stencil_ready = true;
            glEnable(GL_STENCIL_TEST);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glStencilMask(0xffU);
        }
    }

    const auto apply_shader = [&](const VectorDrawPacket& packet) {
        const auto& shader = packet.shader;
        functions.uniform_1i(shader_enabled_uniform_, shader ? 1 : 0);
        if (!shader) return;
        functions.uniform_4f(shader_primary_uniform_, shader->primary_color.red, shader->primary_color.green, shader->primary_color.blue, shader->primary_color.alpha);
        functions.uniform_4f(shader_effect_uniform_, shader->effect_color.red, shader->effect_color.green, shader->effect_color.blue, shader->effect_color.alpha);
        functions.uniform_1f(shader_shine_uniform_, shader->shine);
        functions.uniform_1f(shader_holography_uniform_, shader->holography);
        functions.uniform_1f(shader_opacity_uniform_, shader->opacity);
        functions.uniform_1f(shader_intensity_uniform_, shader->intensity);
    };

    const auto draw_fill = [&](const VectorDrawPacket& packet,
                               const bool stencil_only,
                               const bool count_stats) {
        bool resolved_image = false;
        std::optional<OpenGLTextureHandle> texture;
        if (packet.image_fill.has_value() && !stencil_only) {
            if (!texture_resolver) {
                stats.errors.push_back(
                    "OpenGL vector renderer requires a texture resolver for image fills");
            } else {
                texture = texture_resolver(packet.image_fill->texture.id);
                if (!texture.has_value() || texture->handle == 0U) {
                    stats.errors.push_back(
                        "OpenGL vector texture reference could not be resolved: " +
                        packet.image_fill->texture.id.value);
                }
            }
            resolved_image = texture.has_value() && texture->handle != 0U;
        }
        const bool has_uv = packet.fill_uv.size() == packet.fill_vertices.size();
        if (packet.image_fill.has_value() && !stencil_only && texture.has_value() &&
            !has_uv) {
            stats.errors.push_back("OpenGL image fill UV count does not match silhouette");
        }
        const bool can_draw_fill = !packet.fill_indices.empty() &&
            (stencil_only || (packet.image_fill.has_value()
                ? resolved_image && has_uv
                : packet.fill_color.has_value()));
        if (!can_draw_fill) return false;
        float geometry_width = 0.0F;
        float geometry_height = 0.0F;
        if (packet.image_fill && packet.outline.size() >= 2U) {
            auto minimum = packet.outline.front();
            auto maximum = minimum;
            for (const auto point : packet.outline) {
                minimum.x = std::min(minimum.x, point.x);
                minimum.y = std::min(minimum.y, point.y);
                maximum.x = std::max(maximum.x, point.x);
                maximum.y = std::max(maximum.y, point.y);
            }
            geometry_width = maximum.x - minimum.x;
            geometry_height = maximum.y - minimum.y;
        }
        const float geometry_aspect = geometry_height > 0.0001F
            ? geometry_width / geometry_height : 1.0F;
        const float texture_aspect = texture && texture->height > 0U
            ? static_cast<float>(texture->width) /
                  static_cast<float>(texture->height)
            : 1.0F;
        vertex_scratch_.clear();
        vertex_scratch_.reserve(packet.fill_vertices.size());
        for (std::size_t index = 0; index < packet.fill_vertices.size(); ++index) {
            const auto point = packet.fill_vertices[index];
            const auto uv = has_uv
                ? apply_image_fill_fit(packet.fill_uv[index],
                                       packet.image_fill
                                           ? packet.image_fill->fit
                                           : project::VectorImageFit::stretch,
                                       geometry_aspect, texture_aspect)
                : core::Vec2{};
            vertex_scratch_.push_back({point.x, point.y, uv.x, uv.y});
        }
        upload_buffer(GL_ARRAY_BUFFER, vertex_scratch_.size() * sizeof(Vertex),
                      vertex_scratch_.data(), vertex_buffer_capacity_);
        upload_buffer(GL_ELEMENT_ARRAY_BUFFER,
                      packet.fill_indices.size() * sizeof(std::uint32_t),
                      packet.fill_indices.data(), index_buffer_capacity_);
        if (resolved_image && !stencil_only) {
            const auto tint = texture_tint(packet);
            functions.uniform_4f(color_uniform_, tint.red, tint.green,
                                 tint.blue, tint.alpha);
            functions.uniform_1i(textured_uniform_, 1);
            functions.uniform_1f(opacity_uniform_, packet.image_fill->opacity);
            functions.uniform_1i(image_texture_uniform_, 0);
            functions.active_texture(GL_TEXTURE0);
            functions.bind_texture(GL_TEXTURE_2D, texture->handle);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            packet.repeat_texture_x
                                ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
            if (packet.raster_filter) {
                const auto filter = *packet.raster_filter ==
                        project::RasterFilter::nearest
                    ? GL_NEAREST : GL_LINEAR;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            }
        } else {
            const auto color = stencil_only
                ? core::Color{1.0F, 1.0F, 1.0F, 1.0F}
                : *packet.fill_color;
            functions.uniform_4f(color_uniform_, color.red, color.green,
                                 color.blue, color.alpha);
            functions.uniform_1i(textured_uniform_, 0);
            functions.uniform_1f(opacity_uniform_, 1.0F);
        }
        apply_shader(packet);
        functions.draw_elements(
            GL_TRIANGLES, static_cast<GLsizei>(packet.fill_indices.size()),
            GL_UNSIGNED_INT, nullptr);
        ++stats.draw_calls;
        if (count_stats) {
            stats.triangles_drawn += static_cast<std::uint32_t>(
                packet.fill_indices.size() / 3U);
        }
        return true;
    };

    const auto draw_stroke = [&](const VectorDrawPacket& packet) {
        if (!packet.stroke.has_value() || packet.stroke_vertices.empty() ||
            packet.stroke_indices.empty()) return false;
        vertex_scratch_.clear();
        vertex_scratch_.reserve(packet.stroke_vertices.size());
        const bool has_uv = packet.stroke_uv.size() == packet.stroke_vertices.size();
        for (std::size_t index = 0; index < packet.stroke_vertices.size(); ++index) {
            const auto point = packet.stroke_vertices[index];
            const auto uv = has_uv ? packet.stroke_uv[index] : core::Vec2{};
            vertex_scratch_.push_back({point.x, point.y, uv.x, uv.y});
        }
        upload_buffer(GL_ARRAY_BUFFER, vertex_scratch_.size() * sizeof(Vertex),
                      vertex_scratch_.data(), vertex_buffer_capacity_);
        upload_buffer(GL_ELEMENT_ARRAY_BUFFER,
                      packet.stroke_indices.size() * sizeof(std::uint32_t),
                      packet.stroke_indices.data(), index_buffer_capacity_);
        if (packet.stroke_image && texture_resolver && has_uv) {
            const auto texture = texture_resolver(
                packet.stroke_image->texture.id);
            if (!texture || texture->handle == 0U) {
                stats.errors.push_back(
                    "OpenGL stroke texture reference could not be resolved: " +
                    packet.stroke_image->texture.id.value);
                return false;
            }
            functions.uniform_4f(color_uniform_, packet.stroke->color.red,
                                 packet.stroke->color.green,
                                 packet.stroke->color.blue,
                                 packet.stroke->color.alpha);
            functions.uniform_1i(textured_uniform_, 1);
            functions.uniform_1f(opacity_uniform_, packet.stroke_image->opacity);
            functions.uniform_1i(image_texture_uniform_, 0);
            functions.active_texture(GL_TEXTURE0);
            functions.bind_texture(GL_TEXTURE_2D, texture->handle);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            packet.stroke_repeat_texture_x
                                ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            const auto& color = packet.stroke->color;
            functions.uniform_4f(color_uniform_, color.red, color.green,
                                 color.blue, color.alpha);
            functions.uniform_1i(textured_uniform_, 0);
            functions.uniform_1f(opacity_uniform_, 1.0F);
        }
        apply_shader(packet);
        functions.draw_elements(
            GL_TRIANGLES, static_cast<GLsizei>(packet.stroke_indices.size()),
            GL_UNSIGNED_INT, nullptr);
        ++stats.draw_calls;
        stats.triangles_drawn += static_cast<std::uint32_t>(
            packet.stroke_indices.size() / 3U);
        return true;
    };

    const auto same_fill_material = [](const VectorDrawPacket& left,
                                       const VectorDrawPacket& right) {
        if (left.shader != right.shader) return false;
        if (left.image_fill.has_value() != right.image_fill.has_value()) return false;
        if (left.image_fill) {
            return left.image_fill->texture.id == right.image_fill->texture.id &&
                left.image_fill->opacity == right.image_fill->opacity &&
                left.raster_filter == right.raster_filter &&
                left.repeat_texture_x == right.repeat_texture_x &&
                same_texture_tint(left, right);
        }
        if (left.fill_color.has_value() != right.fill_color.has_value()) return false;
        if (!left.fill_color) return true;
        return left.fill_color->red == right.fill_color->red &&
            left.fill_color->green == right.fill_color->green &&
            left.fill_color->blue == right.fill_color->blue &&
            left.fill_color->alpha == right.fill_color->alpha;
    };

    const auto draw_fill_batch = [&](const std::span<const VectorDrawPacket* const> batch) {
        if (batch.empty()) return false;
        const auto& first = *batch.front();
        std::optional<OpenGLTextureHandle> texture;
        if (first.image_fill) {
            if (!texture_resolver) {
                stats.errors.push_back(
                    "OpenGL vector renderer requires a texture resolver for image fills");
                return false;
            }
            texture = texture_resolver(first.image_fill->texture.id);
            if (!texture || texture->handle == 0U) {
                stats.errors.push_back(
                    "OpenGL vector texture reference could not be resolved: " +
                    first.image_fill->texture.id.value);
                return false;
            }
        } else if (!first.fill_color) {
            return false;
        }

        vertex_scratch_.clear();
        index_scratch_.clear();
        for (const auto* packet : batch) {
            if (packet == nullptr || packet->fill_vertices.empty() ||
                packet->fill_indices.empty() || !same_fill_material(first, *packet))
                return false;
            if (packet->image_fill) {
                const auto resolved = texture_resolver(packet->image_fill->texture.id);
                if (!resolved || resolved->handle != texture->handle) return false;
                if (packet->fill_uv.size() != packet->fill_vertices.size()) {
                    stats.errors.push_back("OpenGL image fill UV count does not match silhouette");
                    return false;
                }
            }
            const auto base = static_cast<std::uint32_t>(vertex_scratch_.size());
            vertex_scratch_.reserve(vertex_scratch_.size() + packet->fill_vertices.size());
            for (std::size_t index = 0; index < packet->fill_vertices.size(); ++index) {
                const auto point = packet->fill_vertices[index];
                const auto uv = packet->fill_uv.size() == packet->fill_vertices.size()
                    ? packet->fill_uv[index] : core::Vec2{};
                vertex_scratch_.push_back({point.x, point.y, uv.x, uv.y});
            }
            index_scratch_.reserve(index_scratch_.size() + packet->fill_indices.size());
            for (const auto index : packet->fill_indices) index_scratch_.push_back(base + index);
        }

        upload_buffer(GL_ARRAY_BUFFER, vertex_scratch_.size() * sizeof(Vertex),
                      vertex_scratch_.data(), vertex_buffer_capacity_);
        upload_buffer(GL_ELEMENT_ARRAY_BUFFER, index_scratch_.size() * sizeof(std::uint32_t),
                      index_scratch_.data(),
                      index_buffer_capacity_);
        if (texture) {
            const auto tint = texture_tint(first);
            functions.uniform_4f(color_uniform_, tint.red, tint.green,
                                 tint.blue, tint.alpha);
            functions.uniform_1i(textured_uniform_, 1);
            functions.uniform_1f(opacity_uniform_, first.image_fill->opacity);
            functions.uniform_1i(image_texture_uniform_, 0);
            functions.active_texture(GL_TEXTURE0);
            functions.bind_texture(GL_TEXTURE_2D, texture->handle);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            first.repeat_texture_x
                                ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
            if (first.raster_filter) {
                const auto filter = *first.raster_filter ==
                        project::RasterFilter::nearest
                    ? GL_NEAREST : GL_LINEAR;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            }
        } else {
            const auto& color = *first.fill_color;
            functions.uniform_4f(color_uniform_, color.red, color.green, color.blue, color.alpha);
            functions.uniform_1i(textured_uniform_, 0);
            functions.uniform_1f(opacity_uniform_, 1.0F);
        }
        apply_shader(first);
        functions.draw_elements(GL_TRIANGLES, static_cast<GLsizei>(index_scratch_.size()),
                                GL_UNSIGNED_INT, nullptr);
        ++stats.draw_calls;
        stats.packets_drawn += static_cast<std::uint32_t>(batch.size());
        stats.triangles_drawn += static_cast<std::uint32_t>(index_scratch_.size() / 3U);
        return true;
    };

    for (std::size_t packet_index = 0; packet_index < packets.size();) {
        const auto& packet = packets[packet_index];
        if (!packet.clip_node_id && !packet.stroke && !packet.fill_indices.empty()) {
            batch_scratch_.clear();
            batch_scratch_.push_back(&packet);
            std::size_t next = packet_index + 1U;
            while (next < packets.size()) {
                const auto& candidate = packets[next];
                if (candidate.clip_node_id || candidate.stroke || candidate.fill_indices.empty() ||
                    !same_fill_material(packet, candidate)) break;
                batch_scratch_.push_back(&candidate);
                ++next;
            }
            if (batch_scratch_.size() > 1U &&
                draw_fill_batch(std::span<const VectorDrawPacket* const>(batch_scratch_))) {
                packet_index = next;
                continue;
            }
        }
        bool packet_drawn = false;
        if (packet.clip_node_id.has_value()) {
            std::vector<const VectorDrawPacket*> clip_chain;
            std::unordered_set<std::string> visited_clips;
            auto next_clip_id = packet.clip_node_id;
            bool clip_chain_valid = true;
            while (next_clip_id.has_value()) {
                if (!visited_clips.insert(*next_clip_id).second) {
                    stats.errors.push_back("OpenGL vector clip cycle detected at: " +
                                           *next_clip_id);
                    clip_chain_valid = false;
                    break;
                }
                const auto clip = packets_by_id.find(*next_clip_id);
                if (clip == packets_by_id.end()) {
                    stats.errors.push_back("OpenGL clip node could not be resolved: " +
                                           *next_clip_id);
                    clip_chain_valid = false;
                    break;
                }
                clip_chain.push_back(clip->second);
                next_clip_id = clip->second->clip_node_id;
            }
            if (!clip_chain_valid || clip_chain.empty()) {
                ++packet_index;
                continue;
            }
            std::ranges::reverse(clip_chain);
            if (!stencil_ready) {
                const auto clipped = cpu_clip_packet(packet, clip_chain);
                if (!clipped) {
                    stats.errors.push_back(
                        "CPU vector clipping requires convex clip silhouettes");
                    ++packet_index;
                    continue;
                }
                packet_drawn = draw_fill(*clipped, false, true) ||
                    draw_stroke(*clipped);
                if (packet_drawn) ++stats.packets_drawn;
                ++packet_index;
                continue;
            }
            glClearStencil(0);
            glClear(GL_STENCIL_BUFFER_BIT);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            bool clip_drawn = true;
            for (std::size_t level = 0; level < clip_chain.size(); ++level) {
                const auto reference = static_cast<GLint>(level + 1U);
                if (level == 0U) {
                    glStencilFunc(GL_ALWAYS, reference, 0xffU);
                } else {
                    glStencilFunc(GL_EQUAL, static_cast<GLint>(level), 0xffU);
                }
                glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
                clip_drawn = draw_fill(*clip_chain[level], true, false) &&
                    clip_drawn;
                if (!clip_drawn) break;
            }
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            if (!clip_drawn) {
                stats.errors.push_back("OpenGL clip node has no fill geometry: " +
                                       clip_chain.back()->node_id);
                ++packet_index;
                continue;
            }
            glStencilFunc(GL_EQUAL,
                          static_cast<GLint>(clip_chain.size()), 0xffU);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }
        packet_drawn = draw_fill(packet, false, true) || packet_drawn;
        packet_drawn = draw_stroke(packet) || packet_drawn;
        if (packet_drawn) ++stats.packets_drawn;
        ++packet_index;
    }
    if (stencil_ready) glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    if (use_vertex_array_) functions.bind_vertex_array(0U);
    functions.use_program(0U);
    return stats;
}

} // namespace fabric::render
