#pragma once

#include "fabric/core/types.hpp"
#include "fabric/core/resource_id.hpp"
#include "fabric/render/vector_geometry.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fabric::render {

struct OpenGLVectorViewport {
    std::int32_t width{};
    std::int32_t height{};
    core::Rect world_bounds;
    std::int32_t x{};
    std::int32_t y{};
};

struct OpenGLVectorRenderStats {
    std::uint32_t packets_submitted{};
    std::uint32_t packets_drawn{};
    std::uint32_t draw_calls{};
    std::uint32_t triangles_drawn{};
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

struct OpenGLTextureHandle {
    std::uint32_t handle{};
    std::uint32_t width{};
    std::uint32_t height{};
};

using OpenGLTextureResolver = std::function<std::optional<OpenGLTextureHandle>(
    const core::ResourceId&)>;

class OpenGLVectorRenderer {
public:
    OpenGLVectorRenderer() = default;
    OpenGLVectorRenderer(const OpenGLVectorRenderer&) = delete;
    OpenGLVectorRenderer& operator=(const OpenGLVectorRenderer&) = delete;
    ~OpenGLVectorRenderer();

    [[nodiscard]] bool initialize();
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return program_ != 0U; }
    [[nodiscard]] const std::string& initialization_error() const noexcept {
        return initialization_error_;
    }
    [[nodiscard]] OpenGLVectorRenderStats draw(
        std::span<const VectorDrawPacket> packets,
        const OpenGLVectorViewport& viewport,
        const OpenGLTextureResolver& texture_resolver = {});

private:
    struct Vertex {
        float x;
        float y;
        float u;
        float v;
    };

    std::uint32_t program_{};
    std::uint32_t vertex_array_{};
    bool use_vertex_array_{};
    std::uint32_t vertex_buffer_{};
    std::uint32_t index_buffer_{};
    std::int32_t world_to_clip_uniform_{-1};
    std::int32_t color_uniform_{-1};
    std::int32_t image_texture_uniform_{-1};
    std::int32_t textured_uniform_{-1};
    std::int32_t opacity_uniform_{-1};
    std::string initialization_error_;
    std::size_t vertex_buffer_capacity_{};
    std::size_t index_buffer_capacity_{};
    std::vector<Vertex> vertex_scratch_;
    std::vector<std::uint32_t> index_scratch_;
    std::vector<const VectorDrawPacket*> batch_scratch_;
};

} // namespace fabric::render
