#include "VocalTransport.hpp"

#include <algorithm>
#include <cmath>

namespace vocalrack {

void ClockEstimator::reset() noexcept {
    high_ = false; samplesSinceEdge_ = 0; edgeCount_ = 0; intervalCount_ = 0; nextInterval_ = 0; estimatedBpm_ = 0.0;
    outlierBpm_ = 0.0; consecutiveOutliers_ = 0;
}

bool ClockEstimator::process(float voltage, double sampleRate, int ppqn) noexcept {
    if (!std::isfinite(voltage)) voltage = 0.f;
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) return false;
    ++samplesSinceEdge_;
    const bool nowHigh = high_ ? voltage >= 0.1f : voltage >= 1.f;
    const bool edge = nowHigh && !high_;
    high_ = nowHigh;
    if (!edge) return false;
    ++edgeCount_;
    if (edgeCount_ > 1 && samplesSinceEdge_ > 1) {
        const double seconds = samplesSinceEdge_ / sampleRate;
        const double candidate = 60.0 / (seconds * std::max(1, ppqn));
        bool accept = candidate >= 20.0 && candidate <= 400.0;
        if (accept && estimatedBpm_ > 0.0 && !(candidate > estimatedBpm_ * 0.55 && candidate < estimatedBpm_ * 1.8)) {
            // A phase discontinuity can produce one half/double-tempo interval.
            // Require three consistent outliers before treating it as a real tempo jump.
            if (outlierBpm_ > 0.0 && candidate > outlierBpm_ * 0.95 && candidate < outlierBpm_ * 1.05)
                ++consecutiveOutliers_;
            else { outlierBpm_ = candidate; consecutiveOutliers_ = 1; }
            if (consecutiveOutliers_ >= 3) {
                intervalCount_ = 0; nextInterval_ = 0; estimatedBpm_ = 0.0;
                consecutiveOutliers_ = 0; outlierBpm_ = 0.0;
            }
            else accept = false;
        }
        if (accept) {
            consecutiveOutliers_ = 0; outlierBpm_ = 0.0;
            intervals_[nextInterval_] = candidate; nextInterval_ = (nextInterval_ + 1) % intervals_.size();
            intervalCount_ = std::min(intervalCount_ + 1, intervals_.size());
            // GCC 16's std::sort inliner can diagnose the valid one-past-end
            // iterator for a full std::array as an out-of-bounds subscript.
            // Eight values are cheaper and clearer to insertion-sort here;
            // .at() also leaves the bounded audio-thread invariant explicit.
            auto sorted = intervals_;
            for (size_t index = 1; index < intervalCount_; ++index) {
                const double value = sorted.at(index);
                size_t insert = index;
                while (insert > 0 && sorted.at(insert - 1) > value) {
                    sorted.at(insert) = sorted.at(insert - 1);
                    --insert;
                }
                sorted.at(insert) = value;
            }
            estimatedBpm_ = sorted.at(intervalCount_ / 2);
        }
    }
    samplesSinceEdge_ = 0;
    return true;
}

double ClockEstimator::bpm(double fallback) const noexcept { return estimatedBpm_ > 0.0 ? estimatedBpm_ : fallback; }

void VocalTransport::setScore(const VocalScore* score) noexcept {
    score_ = score;
    // Score snapshots are immutable while the audio thread owns them, so the
    // endpoint can be computed once on publication instead of scanning every
    // note for every audio sample.
    songEndTick_ = score_ ? score_->endTick() : 0;
    activeSection_ = settings.selectedSection; clock_.reset(); reset();
}

std::pair<int64_t, int64_t> VocalTransport::activeRange() const noexcept {
    if (!score_) return {0, 0};
    if (settings.rangeMode == RangeMode::Section && !score_->sections.empty()) {
        const auto& section = score_->sections[std::min(activeSection_, score_->sections.size() - 1)];
        return {section.startTick, section.endTick};
    }
    return {0, songEndTick_};
}

void VocalTransport::reset() noexcept {
    const auto range = activeRange(); playheadTick_ = range.first; previousWholeTick_ = range.first;
    // Keep the clock estimator warm while stopped/reset so an armed restart
    // begins at the already measured external tempo instead of the BPM fallback.
    sectionPending_ = false; phaseCorrectionTicks_ = 0.0;
}

void VocalTransport::trigger() noexcept { reset(); running_ = true; armed_ = false; }

void VocalTransport::requestSection(size_t index) noexcept {
    if (!score_ || score_->sections.empty()) return;
    index = std::min(index, score_->sections.size() - 1);
    settings.selectedSection = index;
    if (index == activeSection_) return;
    pendingSection_ = index; sectionPending_ = true;
    if (settings.sectionQuantization == SectionQuantization::Immediate) applyPendingSection();
}

void VocalTransport::applyPendingSection() noexcept {
    if (!sectionPending_) return;
    activeSection_ = pendingSection_; sectionPending_ = false;
    const auto range = activeRange(); playheadTick_ = range.first; previousWholeTick_ = range.first;
    phaseCorrectionTicks_ = 0.0;
}

TransportOutput VocalTransport::process(const TransportInput& input, double sampleRate) noexcept {
    TransportOutput out;
    if (!score_ || !std::isfinite(sampleRate) || sampleRate <= 0.0) return out;
    settings.ppqn = std::clamp(settings.ppqn, 1, 48);
    if (settings.rangeMode == RangeMode::Section && score_->sections.empty()) settings.rangeMode = RangeMode::Song;
    const bool clockEdge = clock_.process(input.clock, sampleRate, settings.ppqn);
    const bool resetNow = input.reset >= 1.f;
    if (resetNow && !resetHigh_) reset();
    resetHigh_ = resetNow;
    const bool trigNow = input.trig >= 1.f;
    if (trigNow && !trigHigh_ && (!input.runConnected || input.run >= 1.f)) trigger();
    trigHigh_ = trigNow;

    const bool requestedRun = input.runConnected ? input.run >= 1.f : input.panelPlaying;
    const bool rising = requestedRun && !runWasHigh_;
    runWasHigh_ = requestedRun;
    if (!requestedRun) running_ = false;
    else if (rising) {
        if (settings.runRising == RunRisingBehavior::Restart || armed_) reset();
        running_ = true; armed_ = false;
    } else if (!armed_) running_ = true;

    const auto range = activeRange();
    const double fallbackBpm = std::isfinite(settings.internalBpm) && settings.internalBpm >= 20.0
        && settings.internalBpm <= 400.0 ? settings.internalBpm : 120.0;
    const double bpm = input.clockConnected ? clock_.bpm(fallbackBpm) : fallbackBpm;
    if (input.clockConnected && clockEdge && running_ && clock_.edgeCount() > 1) {
        const double pulseTicks = kTicksPerQuarter / static_cast<double>(settings.ppqn);
        const double nearestPulse = range.first + std::round((playheadTick_ - range.first) / pulseTicks) * pulseTicks;
        const double error = nearestPulse - playheadTick_;
        // Never snap the audio playhead. Accumulate at most a quarter pulse of
        // error and bleed it in at 2.5% of normal transport speed.
        phaseCorrectionTicks_ = std::clamp(phaseCorrectionTicks_ + error,
            -pulseTicks * 0.25, pulseTicks * 0.25);
    }
    if (running_ && range.second > range.first) {
        previousWholeTick_ = static_cast<int64_t>(std::floor(playheadTick_));
        const double baseAdvance = bpm * kTicksPerQuarter / (60.0 * sampleRate);
        const double correction = std::clamp(phaseCorrectionTicks_, -baseAdvance * 0.025, baseAdvance * 0.025);
        playheadTick_ += baseAdvance + correction;
        phaseCorrectionTicks_ -= correction;
        if (sectionPending_) {
            const auto now = static_cast<int64_t>(std::floor(playheadTick_));
            bool switchNow = false;
            if (settings.sectionQuantization == SectionQuantization::NextBeat)
                switchNow = now / kTicksPerQuarter != previousWholeTick_ / kTicksPerQuarter;
            else if (settings.sectionQuantization == SectionQuantization::NextBar) {
                const int64_t bar = kTicksPerQuarter * std::max(1, score_->beatsPerBar);
                switchNow = now / bar != previousWholeTick_ / bar;
            }
            if (switchNow) applyPendingSection();
        }
        if (playheadTick_ >= range.second) {
            out.endPulse = out.rangeCompleted = true;
            if (sectionPending_ && settings.sectionQuantization == SectionQuantization::EndOfSection) applyPendingSection();
            else if (settings.loop) playheadTick_ = range.first + std::fmod(playheadTick_ - range.first, range.second - range.first);
            else { playheadTick_ = range.first; running_ = false; armed_ = true; }
        }
    }
    out.playheadTick = playheadTick_; out.running = running_; out.armed = armed_; out.effectiveBpm = bpm;
    out.selectedSection = activeSection_; return out;
}

}  // namespace vocalrack
