#include "fabric/runtime/audio_mixer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {

void word(std::vector<std::uint8_t>& bytes, const std::size_t offset,
          const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index)
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
}

void short_word(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                const std::int16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

std::filesystem::path wav_fixture() {
    const auto path = std::filesystem::temp_directory_path() /
        ("fabric-audio-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    std::vector<std::uint8_t> bytes(48, 0);
    const auto copy = [&](const std::size_t offset, const char* text) {
        for (std::size_t index = 0; text[index] != '\0'; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(text[index]);
    };
    copy(0, "RIFF"); word(bytes, 4, 40); copy(8, "WAVE"); copy(12, "fmt ");
    word(bytes, 16, 16); bytes[20] = 1; bytes[22] = 1;
    word(bytes, 24, 8000); word(bytes, 28, 16000); bytes[32] = 2; bytes[34] = 16;
    copy(36, "data"); word(bytes, 40, 4); short_word(bytes, 44, 1000); short_word(bytes, 46, -1000);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

TEST_CASE("PCM WAV loads and software mixer mixes and expires voices") {
    const auto path = wav_fixture();
    const auto loaded = fabric::runtime::load_pcm_wav(path);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.clip->sample_rate == 8000);
    REQUIRE(loaded.clip->samples.size() == 2);

    fabric::runtime::PcmAudioMixer mixer;
    REQUIRE(mixer.configure(8000, 1));
    REQUIRE(mixer.play(*loaded.clip, 0.5F));
    const auto first = mixer.mix(1);
    REQUIRE(first.size() == 1);
    CHECK(first[0] == 500);
    CHECK(mixer.voice_count() == 1);
    const auto second = mixer.mix(1);
    CHECK(second[0] == -500);
    CHECK(mixer.voice_count() == 0);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("mixer rejects incompatible clips and clamps output") {
    fabric::runtime::PcmAudioMixer mixer;
    REQUIRE(mixer.configure(44100, 2));
    const fabric::runtime::PcmWavClip clip{.sample_rate = 44100, .channels = 1,
                                           .samples = {30000, 30000}};
    REQUIRE(mixer.play(clip, 2.0F));
    const auto output = mixer.mix(1);
    REQUIRE(output.size() == 2);
    CHECK(output[0] == 32767);
    CHECK(output[1] == 32767);
}

} // namespace
