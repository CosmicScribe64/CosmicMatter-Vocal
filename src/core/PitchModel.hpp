#pragma once

#include "VocalScore.hpp"

#include <algorithm>
#include <cmath>

namespace vocalrack {

constexpr double kDefaultPortamentoHalfMs = 40.0;

inline int64_t defaultPortamentoHalfTicks(double bpm) noexcept {
    const double safeBpm = bpm > 0.0 && std::isfinite(bpm) ? bpm : 120.0;
    return std::max<int64_t>(1, static_cast<int64_t>(std::llround(
        kDefaultPortamentoHalfMs * safeBpm * kTicksPerQuarter / 60000.0)));
}

inline int64_t pitchContourStartOffset(const VocalScore& score, size_t noteIndex,
                                       double bpm) noexcept {
    if (noteIndex >= score.notes.size()) return 0;
    const auto& note = score.notes[noteIndex];
    if (!note.pitchCents.points.empty())
        return std::min<int64_t>(0, note.pitchCents.points.front().tickOffset);
    if (note.pitchSnapFirst && noteIndex > 0 &&
        score.notes[noteIndex - 1].endTick() == note.startTick)
        return -defaultPortamentoHalfTicks(bpm);
    return 0;
}

inline int64_t pitchContourEndOffset(const Note& note) noexcept {
    if (note.pitchCents.points.empty()) return note.durationTick;
    return std::max(note.durationTick, note.pitchCents.points.back().tickOffset);
}

// OpenUtau's fresh-install Standard portamento is 80 ms long and centered on
// an adjacent note boundary. The current note begins at the predecessor's
// nominal pitch and eases to its own pitch. A rest/gap has no connection.
inline float implicitPortamentoCents(const VocalScore& score, size_t noteIndex,
                                     int64_t tickOffset, double bpm) noexcept {
    if (noteIndex == 0 || noteIndex >= score.notes.size()) return 0.f;
    const auto& note = score.notes[noteIndex];
    const auto& previous = score.notes[noteIndex - 1];
    if (!note.pitchSnapFirst || previous.endTick() != note.startTick) return 0.f;
    const float from = static_cast<float>((previous.midiNote - note.midiNote) * 100);
    if (from == 0.f) return 0.f;
    const int64_t half = defaultPortamentoHalfTicks(bpm);
    if (tickOffset <= -half) return from;
    if (tickOffset >= half) return 0.f;
    const double alpha = (tickOffset + half) / static_cast<double>(half * 2);
    constexpr double pi = 3.14159265358979323846;
    const double eased = (1.0 - std::cos(std::clamp(alpha, 0.0, 1.0) * pi)) * 0.5;
    return static_cast<float>(from * (1.0 - eased));
}

inline float notePitchCents(const VocalScore& score, size_t noteIndex,
                            int64_t tickOffset, double bpm) noexcept {
    if (noteIndex >= score.notes.size()) return 0.f;
    const auto& note = score.notes[noteIndex];
    if (note.pitchCents.points.empty())
        return implicitPortamentoCents(score, noteIndex, tickOffset, bpm);
    if (!note.pitchSnapFirst)
        return note.pitchCents.sample(tickOffset, 0.f);

    // This is OpenUtau UNote.Validate's snap-first rule: only the first point's
    // Y is replaced by the adjacent predecessor's relative tone. Subsequent
    // authored points remain unchanged. USTX imports are densely sampled to
    // preserve their curve shape, so linear interpolation here is lossless at
    // that cadence and native points behave predictably.
    const auto& points = note.pitchCents.points;
    float snapped = 0.f;
    if (noteIndex > 0 && score.notes[noteIndex - 1].endTick() == note.startTick)
        snapped = static_cast<float>(
            (score.notes[noteIndex - 1].midiNote - note.midiNote) * 100);
    if (tickOffset <= points.front().tickOffset || points.size() == 1) return snapped;
    if (tickOffset >= points[1].tickOffset)
        return note.pitchCents.sample(tickOffset, 0.f);
    const auto& next = points[1];
    const double span = static_cast<double>(next.tickOffset - points.front().tickOffset);
    const double alpha = span > 0.0
        ? (tickOffset - points.front().tickOffset) / span : 1.0;
    return static_cast<float>(snapped + (next.value - snapped) * std::clamp(alpha, 0.0, 1.0));
}

inline float noteVibratoCents(const Note& note, int64_t tickOffset,
                              double bpm) noexcept {
    if (tickOffset < 0 || note.durationTick <= 0 || note.vibrato.depthCents == 0.f) return 0.f;
    const double progress = std::clamp(
        tickOffset / static_cast<double>(note.durationTick), 0.0, 1.0);
    const double start = std::clamp(note.vibrato.startPercent / 100.0, 0.0, 1.0);
    if (progress < start) return 0.f;
    const double length = std::max(0.01, 1.0 - start);
    const double vibratoProgress = (progress - start) / length;
    const double fadeIn = std::min(1.0, vibratoProgress * 100.0 /
        std::max(1.f, note.vibrato.fadeInPercent));
    const double fadeOut = std::min(1.0, (1.0 - vibratoProgress) * 100.0 /
        std::max(1.f, note.vibrato.fadeOutPercent));
    const double safeBpm = bpm > 0.0 && std::isfinite(bpm) ? bpm : 120.0;
    const double durationSeconds = note.durationTick * 60.0 /
        (safeBpm * kTicksPerQuarter);
    const double elapsed = vibratoProgress * durationSeconds * length;
    constexpr double twoPi = 6.28318530717958647692;
    return static_cast<float>(note.vibrato.depthCents *
        std::sin(twoPi * note.vibrato.rateHz * elapsed + note.vibrato.phase) *
        std::min(fadeIn, fadeOut));
}

inline float performedPitchCents(const VocalScore& score, size_t noteIndex,
                                 int64_t tickOffset, double bpm) noexcept {
    if (noteIndex >= score.notes.size()) return 0.f;
    return notePitchCents(score, noteIndex, tickOffset, bpm) +
        noteVibratoCents(score.notes[noteIndex], tickOffset, bpm);
}

// Resolve pitch on the score timeline, including a following note's
// negative-time portamento. OpenUtau evaluates those incoming pitch points
// over the tail of the previous adjacent note; switching ownership only at
// the authored boundary would make a centered bend play late.
inline float performedAbsoluteMidi(const VocalScore& score, int64_t absoluteTick,
                                   double bpm) noexcept {
    if (score.notes.empty()) return 60.f;
    size_t noteIndex = 0;
    while (noteIndex + 1 < score.notes.size() &&
           score.notes[noteIndex + 1].startTick <= absoluteTick) {
        ++noteIndex;
    }
    if (noteIndex + 1 < score.notes.size()) {
        const size_t incoming = noteIndex + 1;
        const auto& next = score.notes[incoming];
        const int64_t contourStart = next.startTick +
            pitchContourStartOffset(score, incoming, bpm);
        if (absoluteTick >= contourStart && absoluteTick < next.startTick)
            noteIndex = incoming;
    }
    const auto& note = score.notes[noteIndex];
    const int64_t offset = absoluteTick - note.startTick;
    return note.midiNote + performedPitchCents(score, noteIndex, offset, bpm) / 100.f;
}

}  // namespace vocalrack
