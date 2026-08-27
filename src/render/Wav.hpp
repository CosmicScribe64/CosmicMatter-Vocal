#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vocalrack {

struct AudioBuffer {
    uint32_t sampleRate = 48000;
    std::vector<float> samples;
};

AudioBuffer readWavMono(const std::filesystem::path& path);
void writeWavMono16(const std::filesystem::path& path, const AudioBuffer& audio);
double estimateFundamental(const AudioBuffer& audio, size_t begin = 0, size_t end = 0);
bool finiteAudio(const AudioBuffer& audio) noexcept;
float peakAudio(const AudioBuffer& audio) noexcept;
double rmsAudio(const AudioBuffer& audio) noexcept;

}  // namespace vocalrack

