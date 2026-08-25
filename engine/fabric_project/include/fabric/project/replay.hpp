#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_replay_schema_version = 1;
inline constexpr std::int64_t replay_position_quantization = 4096;
inline constexpr std::int64_t replay_rotation_quantization = 65536;

struct ReplayInput {
    std::uint64_t frame{};
    std::string action;
    bool pressed{};
    bool released{};
    friend bool operator==(const ReplayInput&, const ReplayInput&) = default;
};

struct ReplayEvent {
    std::uint64_t frame{};
    std::string name;
    std::string payload;
    friend bool operator==(const ReplayEvent&, const ReplayEvent&) = default;
};

struct ReplayEntityState {
    std::string node_id;
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t rotation{};
    friend bool operator==(const ReplayEntityState&, const ReplayEntityState&) = default;
};

struct ReplayCheckpoint {
    std::uint64_t frame{};
    std::vector<ReplayEntityState> states;
    friend bool operator==(const ReplayCheckpoint&, const ReplayCheckpoint&) = default;
};

struct ReplayDocument {
    DocumentHeader document{
        .schema_version = current_replay_schema_version,
        .type = "replay",
    };
    std::string build;
    std::uint64_t seed{};
    std::optional<ResourceReference> source_scene;
    std::vector<ReplayInput> inputs;
    std::vector<ReplayEvent> events;
    std::vector<ReplayCheckpoint> checkpoints;
    friend bool operator==(const ReplayDocument&, const ReplayDocument&) = default;
};

struct ReplayResult {
    std::optional<ReplayDocument> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return asset.has_value() && errors.empty(); }
};

[[nodiscard]] std::int64_t quantize_replay_position(float value) noexcept;
[[nodiscard]] float dequantize_replay_position(std::int64_t value) noexcept;
[[nodiscard]] std::int64_t quantize_replay_rotation(float turns) noexcept;
[[nodiscard]] float dequantize_replay_rotation(std::int64_t value) noexcept;

[[nodiscard]] std::filesystem::path replay_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_replay(const ProjectManifest&, const ReplayDocument&);
[[nodiscard]] std::vector<ResourceReference> replay_resource_references(const ReplayDocument&);
[[nodiscard]] std::string serialize_replay(const ReplayDocument&);
[[nodiscard]] ReplayResult parse_replay(const ProjectManifest&, std::string_view);
[[nodiscard]] ReplayResult load_replay(const std::filesystem::path&, const ProjectManifest&,
                                       const std::filesystem::path&);
[[nodiscard]] ReplayResult publish_replay(const std::filesystem::path&, const ProjectManifest&,
                                          const ReplayDocument&);

} // namespace fabric::project
