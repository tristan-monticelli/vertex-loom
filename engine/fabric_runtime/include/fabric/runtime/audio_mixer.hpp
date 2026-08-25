#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fabric::runtime {

struct PcmWavClip {
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::vector<std::int16_t> samples;
};

struct AudioResult {
    std::optional<PcmWavClip> clip;
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return clip.has_value() && error.empty(); }
};

[[nodiscard]] AudioResult load_pcm_wav(const std::filesystem::path&);

class PcmAudioMixer {
public:
    [[nodiscard]] bool configure(std::uint32_t sample_rate,
                                  std::uint16_t channels) noexcept;
    [[nodiscard]] bool play(const PcmWavClip&, float gain = 1.0F) noexcept;
    [[nodiscard]] std::vector<std::int16_t> mix(std::size_t frames) noexcept;
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] std::uint16_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::size_t voice_count() const noexcept { return voices_.size(); }

private:
    struct Voice {
        const PcmWavClip* clip{};
        std::size_t frame{};
        float gain{1.0F};
    };

    std::uint32_t sample_rate_{};
    std::uint16_t channels_{};
    std::vector<Voice> voices_;
};

} // namespace fabric::runtime
