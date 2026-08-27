#pragma once

#include "core/VocalScore.hpp"
#include "phonemizer/Phonemizer.hpp"
#include "render/Wav.hpp"
#include "voicebank/Voicebank.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace vocalrack {

struct RenderOptions {
    uint32_t sampleRate = 48000;
    double bpm = 0.0;
    int transposeSemitones = 0;
    int64_t startTick = 0;
    int64_t endTick = -1;
    std::string phonemizer = kEnglishXSampaPhonemizer;
    // Module-side publication serial. The render cache key excludes it, which
    // lets a valid cached buffer satisfy a newer request.
    uint64_t requestSerial = 0;
};

struct RenderDiagnostics {
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::vector<PhonemeEvent> phonemes;
    double renderSeconds = 0.0;
};

struct RenderedAudio : AudioBuffer {
    int64_t startTick = 0;
    int64_t endTick = 0;
    double bpm = 120.0;
    uint64_t scoreRevision = 0;
    RenderDiagnostics diagnostics;
};

class NativeV1Renderer {
public:
    RenderedAudio render(const VocalScore& score, const Voicebank& singer, const RenderOptions& options,
                         const std::atomic<bool>* canceled = nullptr) const;
};

uint64_t decodedSampleBytes();

}  // namespace vocalrack
