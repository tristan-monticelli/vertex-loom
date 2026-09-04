#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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

struct SpatialAudioMix {
    float attenuation{1.0F};
    float pan{};
};

[[nodiscard]] SpatialAudioMix resolve_spatial_audio(
    float source_x, float source_y, float minimum_distance,
    float maximum_distance, float listener_x = 0.0F,
    float listener_y = 0.0F) noexcept;

class PcmAudioMixer {
public:
    [[nodiscard]] bool configure(std::uint32_t sample_rate,
                                  std::uint16_t channels) noexcept;
    [[nodiscard]] bool play(const PcmWavClip&, float gain = 1.0F) noexcept;
    [[nodiscard]] bool set_bus_gain(std::string bus, float gain);
    [[nodiscard]] bool play(const PcmWavClip&, std::string_view bus,
                            float gain, float pan, bool loop = false) noexcept;
    [[nodiscard]] std::vector<std::int16_t> mix(std::size_t frames) noexcept;
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] std::uint16_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::size_t voice_count() const noexcept { return voices_.size(); }

private:
    struct Voice {
        const PcmWavClip* clip{};
        std::size_t frame{};
        float gain{1.0F};
        float pan{};
        std::string bus{"master"};
        bool loop{};
    };

    std::uint32_t sample_rate_{};
    std::uint16_t channels_{};
    std::vector<Voice> voices_;
    std::unordered_map<std::string, float> bus_gains_{{"master", 1.0F}};
};

class PcmAudioDevice {
public:
    PcmAudioDevice() noexcept = default;
    ~PcmAudioDevice();
    PcmAudioDevice(const PcmAudioDevice&) = delete;
    PcmAudioDevice& operator=(const PcmAudioDevice&) = delete;

    [[nodiscard]] bool open(std::uint32_t sample_rate, std::uint16_t channels,
                            std::uint16_t buffer_frames = 1024) noexcept;
    [[nodiscard]] bool submit(std::span<const std::int16_t> samples) noexcept;
    void close() noexcept;
    [[nodiscard]] bool opened() const noexcept { return device_id_ != 0U; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    std::uint32_t device_id_{};
    std::string error_;
};

} // namespace fabric::runtime
