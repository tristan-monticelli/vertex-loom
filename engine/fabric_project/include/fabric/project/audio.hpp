#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_audio_schema_version = 1;

struct AudioEvent {
    std::string id;
    std::string source;
    float volume{1.0F};
    bool loop{};
    friend bool operator==(const AudioEvent&, const AudioEvent&) = default;
};

struct AudioDocument {
    DocumentHeader document{.schema_version = current_audio_schema_version,
                            .type = "audio", .name = "Audio"};
    std::vector<AudioEvent> events;
    friend bool operator==(const AudioDocument&, const AudioDocument&) = default;
};

struct AudioResult {
    std::optional<AudioDocument> audio;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return audio.has_value() && errors.empty(); }
};

[[nodiscard]] std::filesystem::path audio_document_path(const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_audio(const ProjectManifest&, const AudioDocument&);
[[nodiscard]] AudioResult parse_audio(std::string_view);
[[nodiscard]] std::string serialize_audio(const AudioDocument&);
[[nodiscard]] AudioResult load_audio(const std::filesystem::path&, const ProjectManifest&, const std::filesystem::path&);
[[nodiscard]] AudioResult publish_audio(const std::filesystem::path&, const ProjectManifest&, const AudioDocument&);

} // namespace fabric::project
