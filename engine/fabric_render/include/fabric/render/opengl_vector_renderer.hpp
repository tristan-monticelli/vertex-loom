#pragma once

#include "fabric/core/types.hpp"
#include "fabric/core/resource_id.hpp"
#include "fabric/render/vector_geometry.hpp"

#include <cstdint>
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
    [[nodiscard]] OpenGLVectorRenderStats draw(
        std::span<const VectorDrawPacket> packets,
        const OpenGLVectorViewport& viewport,
        const OpenGLTextureResolver& texture_resolver = {});

private:
    std::uint32_t program_{};
    std::uint32_t vertex_array_{};
    std::uint32_t vertex_buffer_{};
    std::uint32_t index_buffer_{};
    std::int32_t world_to_clip_uniform_{-1};
};

} // namespace fabric::render
