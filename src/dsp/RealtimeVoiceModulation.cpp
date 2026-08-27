#include "RealtimeVoiceModulation.hpp"

#include <algorithm>
#include <cmath>

namespace vocalrack {
static constexpr double kPi = 3.14159265358979323846;

void RealtimeVoiceModulation::reset() noexcept {
    delay_.fill(0.f); write_ = 0; pitchSemitones_ = dynDb_ = vibSemitones_ = form_ = low_ = dcX_ = dcY_ = 0.f;
    vibratoPhase_ = delayPhase_ = 0.0;
}

float RealtimeVoiceModulation::process(float input, const ModulationControls& c, double sampleRate) noexcept {
    const auto finite = [](float value) noexcept { return std::isfinite(value) ? value : 0.f; };
    if (!std::isfinite(pitchSemitones_) || !std::isfinite(dynDb_) ||
        !std::isfinite(vibSemitones_) || !std::isfinite(form_) ||
        !std::isfinite(low_) || !std::isfinite(dcX_) || !std::isfinite(dcY_) ||
        !std::isfinite(vibratoPhase_) || !std::isfinite(delayPhase_)) reset();
    input = finite(input);
    sampleRate = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate) : 48000.0;
    const float coefficient = 1.f - std::exp(-1.f / static_cast<float>(0.012 * sampleRate));
    const float pitchTarget = std::clamp(finite(c.pitchCv) / 5.f * 12.f * finite(c.pitchAttenuverter), -24.f, 24.f);
    const float dynTarget = std::clamp(finite(c.dynamicsCv) / 5.f * 12.f * finite(c.dynamicsAttenuverter), -24.f, 12.f);
    const float vibTarget = std::clamp(finite(c.vibratoCv) / 5.f * 2.f * finite(c.vibratoAttenuverter), -4.f, 4.f);
    const float formTarget = std::clamp(finite(c.formCv) / 5.f * finite(c.formAttenuverter), -1.f, 1.f);
    pitchSemitones_ += (pitchTarget - pitchSemitones_) * coefficient;
    dynDb_ += (dynTarget - dynDb_) * coefficient;
    vibSemitones_ += (vibTarget - vibSemitones_) * coefficient;
    form_ += (formTarget - form_) * coefficient;

    delay_[write_] = input;
    vibratoPhase_ += 2.0 * kPi * 5.5 / sampleRate;
    if (vibratoPhase_ >= 2.0 * kPi) vibratoPhase_ -= 2.0 * kPi;
    const double semitones = pitchSemitones_ + vibSemitones_ * std::sin(vibratoPhase_);
    const double ratio = std::pow(2.0, semitones / 12.0);
    // Dual-head delay pitch shifter. Crossfading heads bounds resets and keeps the score-time position unchanged.
    const double window = std::min<double>(2048.0, sampleRate * 0.035);
    delayPhase_ += (1.0 - ratio) / window;
    delayPhase_ -= std::floor(delayPhase_);
    auto head = [&](double phase) {
        const double delaySamples = 24.0 + phase * window;
        double read = static_cast<double>(write_) - delaySamples;
        while (read < 0.0) read += kDelaySize;
        const size_t i = static_cast<size_t>(read) % kDelaySize, j = (i + 1) % kDelaySize;
        const float a = static_cast<float>(read - std::floor(read));
        return delay_[i] + (delay_[j] - delay_[i]) * a;
    };
    const double other = std::fmod(delayPhase_ + 0.5, 1.0);
    const float weight = 0.5f - 0.5f * std::cos(static_cast<float>(2.0 * kPi * delayPhase_));
    float shifted = head(delayPhase_) * weight + head(other) * (1.f - weight);
    write_ = (write_ + 1) % kDelaySize;

    // Stable form/timbre tilt (renderer-independent) and dynamics in dB.
    const float cutoff = 0.02f + (form_ + 1.f) * 0.18f;
    low_ += cutoff * (shifted - low_);
    shifted = shifted + form_ * 0.75f * (shifted - low_);
    shifted *= std::pow(10.f, dynDb_ / 20.f);
    const float dc = shifted - dcX_ + 0.995f * dcY_; dcX_ = shifted; dcY_ = dc;
    return std::tanh(dc * 1.1f) * 0.92f;
}

}  // namespace vocalrack
