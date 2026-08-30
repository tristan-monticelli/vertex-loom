#include "fabric/runtime/audio_mixer.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fabric::runtime {

AudioResult load_pcm_wav(const std::filesystem::path& path) {
    AudioResult result;
    SDL_AudioSpec spec{};
    std::uint8_t* raw = nullptr;
    std::uint32_t length = 0;
    if (SDL_LoadWAV(path.string().c_str(), &spec, &raw, &length) == nullptr) {
        result.error = SDL_GetError();
        return result;
    }
    const auto cleanup = [&]() { SDL_FreeWAV(raw); };
    if (spec.format != AUDIO_S16LSB || (spec.channels != 1 && spec.channels != 2) ||
        spec.freq <= 0 || length % (sizeof(std::int16_t) * spec.channels) != 0U) {
        cleanup();
        result.error = "only little-endian signed 16-bit mono/stereo PCM WAV is supported";
        return result;
    }
    const auto sample_count = static_cast<std::size_t>(length / sizeof(std::int16_t));
    PcmWavClip clip{.sample_rate = static_cast<std::uint32_t>(spec.freq),
                    .channels = spec.channels,
                    .samples = std::vector<std::int16_t>(sample_count)};
    std::copy_n(reinterpret_cast<const std::int16_t*>(raw), sample_count,
                clip.samples.begin());
    cleanup();
    result.clip = std::move(clip);
    return result;
}

bool PcmAudioMixer::configure(const std::uint32_t sample_rate,
                              const std::uint16_t channels) noexcept {
    if (sample_rate == 0U || (channels != 1 && channels != 2)) return false;
    sample_rate_ = sample_rate;
    channels_ = channels;
    voices_.clear();
    return true;
}

bool PcmAudioMixer::play(const PcmWavClip& clip, const float gain) noexcept {
    if (sample_rate_ == 0U || channels_ == 0U || clip.sample_rate != sample_rate_ ||
        (clip.channels != 1 && clip.channels != 2) || clip.samples.empty() ||
        !std::isfinite(gain) || gain < 0.0F) return false;
    voices_.push_back({.clip = &clip, .frame = 0, .gain = gain});
    return true;
}

std::vector<std::int16_t> PcmAudioMixer::mix(const std::size_t frames) noexcept {
    std::vector<std::int16_t> output(frames * channels_, 0);
    for (auto& voice : voices_) {
        const auto& clip = *voice.clip;
        const auto clip_frames = clip.samples.size() / clip.channels;
        const auto remaining = clip_frames - voice.frame;
        const auto count = std::min(frames, remaining);
        for (std::size_t frame = 0; frame < count; ++frame) {
            const auto source = (voice.frame + frame) * clip.channels;
            const auto left = static_cast<float>(clip.samples[source]) * voice.gain;
            const auto right = static_cast<float>(clip.samples[source + (clip.channels == 2 ? 1 : 0)]) * voice.gain;
            if (channels_ == 1) {
                const auto mixed = clip.channels == 2 ? (left + right) * 0.5F : left;
                const auto target = static_cast<float>(output[frame]) + mixed;
                output[frame] = static_cast<std::int16_t>(std::clamp(
                    target, static_cast<float>(std::numeric_limits<std::int16_t>::min()),
                    static_cast<float>(std::numeric_limits<std::int16_t>::max())));
            } else {
                const auto left_target = static_cast<float>(output[frame * 2U]) + left;
                const auto right_target = static_cast<float>(output[frame * 2U + 1U]) + right;
                output[frame * 2U] = static_cast<std::int16_t>(std::clamp(
                    left_target, static_cast<float>(std::numeric_limits<std::int16_t>::min()),
                    static_cast<float>(std::numeric_limits<std::int16_t>::max())));
                output[frame * 2U + 1U] = static_cast<std::int16_t>(std::clamp(
                    right_target, static_cast<float>(std::numeric_limits<std::int16_t>::min()),
                    static_cast<float>(std::numeric_limits<std::int16_t>::max())));
            }
        }
        voice.frame += count;
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const auto& voice) {
        return voice.frame >= voice.clip->samples.size() / voice.clip->channels;
    }), voices_.end());
    return output;
}

PcmAudioDevice::~PcmAudioDevice() { close(); }

bool PcmAudioDevice::open(const std::uint32_t sample_rate,
                          const std::uint16_t channels,
                          const std::uint16_t buffer_frames) noexcept {
    close();
    error_.clear();
    if (sample_rate == 0U || (channels != 1 && channels != 2) || buffer_frames == 0U) {
        error_ = "invalid PCM audio device format";
        return false;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        error_ = SDL_GetError();
        return false;
    }
    SDL_AudioSpec desired{.freq = static_cast<int>(sample_rate),
                          .format = AUDIO_S16LSB,
                          .channels = static_cast<Uint8>(channels),
                          .samples = buffer_frames};
    SDL_AudioSpec obtained{};
    const auto device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (device == 0U) {
        error_ = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    if (obtained.freq != desired.freq || obtained.format != desired.format ||
        obtained.channels != desired.channels) {
        SDL_CloseAudioDevice(device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        error_ = "audio device did not provide the requested PCM format";
        return false;
    }
    device_id_ = device;
    SDL_PauseAudioDevice(device_id_, 0);
    return true;
}

bool PcmAudioDevice::submit(const std::span<const std::int16_t> samples) noexcept {
    if (device_id_ == 0U || samples.empty()) return false;
    return SDL_QueueAudio(device_id_, samples.data(),
                          static_cast<Uint32>(samples.size_bytes())) == 0;
}

void PcmAudioDevice::close() noexcept {
    if (device_id_ == 0U) return;
    SDL_CloseAudioDevice(device_id_);
    device_id_ = 0U;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

} // namespace fabric::runtime
