#pragma once

#include <array>
#include <cstddef>

namespace vocalrack {

struct ModulationControls {
    float pitchCv = 0.f, dynamicsCv = 0.f, vibratoCv = 0.f, formCv = 0.f;
    float pitchAttenuverter = 0.f, dynamicsAttenuverter = 0.f;
    float vibratoAttenuverter = 0.f, formAttenuverter = 0.f;
};

class RealtimeVoiceModulation {
public:
    void reset() noexcept;
    float process(float input, const ModulationControls& controls, double sampleRate) noexcept;
    float smoothedDynamicsDb() const noexcept { return dynDb_; }
private:
    static constexpr size_t kDelaySize = 8192;
    std::array<float, kDelaySize> delay_{};
    size_t write_ = 0;
    float pitchSemitones_ = 0.f, dynDb_ = 0.f, vibSemitones_ = 0.f, form_ = 0.f;
    float low_ = 0.f, dcX_ = 0.f, dcY_ = 0.f;
    double vibratoPhase_ = 0.0, delayPhase_ = 0.0;
};

}  // namespace vocalrack

