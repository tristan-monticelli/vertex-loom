#include "fabric/runtime/audio_mixer.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>

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

TEST_CASE("mixer applies named bus gain and stereo pan") {
    fabric::runtime::PcmAudioMixer mixer;
    REQUIRE(mixer.configure(44100, 2));
    REQUIRE(mixer.set_bus_gain("effects", 0.5F));
    const fabric::runtime::PcmWavClip clip{
        .sample_rate = 44100, .channels = 1, .samples = {10000}};
    REQUIRE(mixer.play(clip, "effects", 0.8F, 0.5F));

    const auto output = mixer.mix(1);

    REQUIRE(output.size() == 2U);
    CHECK(output[0] == 2000);
    CHECK(output[1] == 4000);
    CHECK_FALSE(mixer.play(clip, "missing", 1.0F, 0.0F));
    CHECK_FALSE(mixer.play(clip, "effects", 1.0F, 1.1F));
}

TEST_CASE("looping voices fill buffers across clip boundaries") {
    fabric::runtime::PcmAudioMixer mixer;
    REQUIRE(mixer.configure(8000, 1));
    const fabric::runtime::PcmWavClip clip{
        .sample_rate = 8000, .channels = 1, .samples = {100, -100}};
    REQUIRE(mixer.play(clip, "master", 1.0F, 0.0F, true));

    CHECK(mixer.mix(5) ==
          std::vector<std::int16_t>{100, -100, 100, -100, 100});
    CHECK(mixer.voice_count() == 1U);
}

TEST_CASE("spatial audio resolves listener-relative attenuation and pan") {
    const auto close = fabric::runtime::resolve_spatial_audio(1.0F, 0.0F,
                                                               2.0F, 10.0F);
    CHECK(close.attenuation == 1.0F);
    CHECK(close.pan == Catch::Approx(0.1F));

    const auto middle = fabric::runtime::resolve_spatial_audio(6.0F, 0.0F,
                                                                2.0F, 10.0F);
    CHECK(middle.attenuation == Catch::Approx(0.5F));
    CHECK(middle.pan == Catch::Approx(0.6F));

    const auto relative = fabric::runtime::resolve_spatial_audio(
        -10.0F, 0.0F, 2.0F, 10.0F, -2.0F, 0.0F);
    CHECK(relative.attenuation == Catch::Approx(0.25F));
    CHECK(relative.pan == Catch::Approx(-0.8F));

    const auto outside = fabric::runtime::resolve_spatial_audio(
        20.0F, 0.0F, 2.0F, 10.0F);
    CHECK(outside.attenuation == 0.0F);
    CHECK(outside.pan == 1.0F);
}

TEST_CASE("real audio device accepts a non-silent stereo probe", "[.device]") {
    constexpr std::uint32_t sample_rate = 8000U;
    fabric::runtime::PcmAudioDevice device;
    REQUIRE(device.open(sample_rate, 2U, 256U));

    std::vector<std::int16_t> samples(sample_rate / 10U * 2U);
    for (std::size_t frame = 0; frame < samples.size() / 2U; ++frame) {
        const auto phase = static_cast<float>(frame % 20U) / 20.0F;
        const auto sample = static_cast<std::int16_t>(
            phase < 0.5F ? 3500 : -3500);
        samples[frame * 2U] = sample;
        samples[frame * 2U + 1U] = sample;
    }
    REQUIRE(device.submit(samples));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    device.close();
}

} // namespace
