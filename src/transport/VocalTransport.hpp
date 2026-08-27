#pragma once

#include "core/VocalScore.hpp"

#include <array>
#include <cstdint>

namespace vocalrack {

enum class RangeMode { Song = 0, Section = 1 };
enum class RunRisingBehavior { Resume = 0, Restart = 1 };
enum class SectionQuantization { Immediate = 0, NextBeat = 1, NextBar = 2, EndOfSection = 3 };

struct TransportSettings {
    double internalBpm = 120.0;
    int ppqn = 24;
    bool loop = false;
    RangeMode rangeMode = RangeMode::Song;
    size_t selectedSection = 0;
    RunRisingBehavior runRising = RunRisingBehavior::Resume;
    SectionQuantization sectionQuantization = SectionQuantization::EndOfSection;
};

struct TransportInput {
    float clock = 0.f;
    bool clockConnected = false;
    float reset = 0.f;
    float trig = 0.f;
    float run = 0.f;
    bool runConnected = false;
    bool panelPlaying = true;
};

struct TransportOutput {
    double playheadTick = 0.0;
    bool running = false;
    bool armed = false;
    bool endPulse = false;
    bool rangeCompleted = false;
    double effectiveBpm = 120.0;
    size_t selectedSection = 0;
};

class ClockEstimator {
public:
    void reset() noexcept;
    bool process(float voltage, double sampleRate, int ppqn) noexcept;
    double bpm(double fallback) const noexcept;
    uint64_t edgeCount() const noexcept { return edgeCount_; }
private:
    bool high_ = false;
    uint64_t samplesSinceEdge_ = 0, edgeCount_ = 0;
    std::array<double, 8> intervals_{};
    size_t intervalCount_ = 0, nextInterval_ = 0;
    double estimatedBpm_ = 0.0;
    double outlierBpm_ = 0.0;
    unsigned consecutiveOutliers_ = 0;
};

class VocalTransport {
public:
    TransportSettings settings;
    void setScore(const VocalScore* score) noexcept;
    void reset() noexcept;
    void trigger() noexcept;
    void requestSection(size_t index) noexcept;
    TransportOutput process(const TransportInput& input, double sampleRate) noexcept;
    double positionTick() const noexcept { return playheadTick_; }
    bool isRunning() const noexcept { return running_; }

private:
    std::pair<int64_t, int64_t> activeRange() const noexcept;
    void applyPendingSection() noexcept;
    const VocalScore* score_ = nullptr;
    int64_t songEndTick_ = 0;
    ClockEstimator clock_;
    double playheadTick_ = 0.0;
    double phaseCorrectionTicks_ = 0.0;
    bool running_ = false, armed_ = false, resetHigh_ = false, trigHigh_ = false, runWasHigh_ = false;
    size_t activeSection_ = 0, pendingSection_ = 0;
    bool sectionPending_ = false;
    int64_t previousWholeTick_ = 0;
};

}  // namespace vocalrack
