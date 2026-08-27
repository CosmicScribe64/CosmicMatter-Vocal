#include "VocalScore.hpp"

#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>

namespace vocalrack {

float Curve::sample(int64_t tickOffset, float defaultValue) const noexcept {
    if (points.empty()) return defaultValue;
    if (tickOffset <= points.front().tickOffset) return points.front().value;
    if (tickOffset >= points.back().tickOffset) return points.back().value;
    auto upper = std::upper_bound(points.begin(), points.end(), tickOffset,
        [](int64_t t, const CurvePoint& p) { return t < p.tickOffset; });
    const auto& b = *upper;
    const auto& a = *(upper - 1);
    const long double span = static_cast<long double>(b.tickOffset) - a.tickOffset;
    const double alpha = span > 0.0 ? static_cast<double>(
        (static_cast<long double>(tickOffset) - a.tickOffset) / span) : 0.0;
    return static_cast<float>(a.value + (b.value - a.value) * alpha);
}

void Curve::normalize() {
    points.erase(std::remove_if(points.begin(), points.end(), [](const auto& point) {
        return !std::isfinite(point.value);
    }), points.end());
    std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.tickOffset < b.tickOffset;
    });
    points.erase(std::unique(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.tickOffset == b.tickOffset;
    }), points.end());
}

int64_t VocalScore::endTick() const noexcept {
    int64_t result = 0;
    for (const auto& note : notes) result = std::max(result, note.endTick());
    for (const auto& section : sections) result = std::max(result, section.endTick);
    return result;
}

std::vector<std::string> VocalScore::validate() const {
    std::vector<std::string> errors;
    if (!(nominalBpm >= 20.0 && nominalBpm <= 400.0) || !std::isfinite(nominalBpm))
        errors.emplace_back("Nominal BPM must be finite and between 20 and 400");
    if (beatsPerBar <= 0 || beatsPerBar > 32 || beatUnit <= 0 || beatUnit > 32)
        errors.emplace_back("Invalid time signature");
    if (notes.size() > 20000) errors.emplace_back("Score exceeds the 20,000-note limit");
    if (sections.size() > 4096) errors.emplace_back("Score exceeds the section-count limit");
    int64_t previousEnd = -1;
    for (const auto& note : notes) {
        if (note.startTick < 0) errors.emplace_back("Note " + note.id + " starts before tick 0");
        if (note.durationTick <= 0) errors.emplace_back("Note " + note.id + " must have positive duration");
        if (note.durationTick > 0 && note.startTick > INT64_MAX - note.durationTick)
            errors.emplace_back("Note " + note.id + " end tick overflows");
        if (note.midiNote < 0 || note.midiNote > 127) errors.emplace_back("Note " + note.id + " has invalid MIDI pitch");
        if (note.phonemeOverrides.size() > 256) errors.emplace_back("Note " + note.id + " has too many phoneme overrides");
        const auto finiteCurve = [](const Curve& curve) {
            return curve.points.size() <= 1000000 && std::all_of(curve.points.begin(), curve.points.end(),
                [](const CurvePoint& point) { return std::isfinite(point.value); });
        };
        if (!finiteCurve(note.pitchCents) || !finiteCurve(note.dynamicsDb))
            errors.emplace_back("Note " + note.id + " has an invalid or oversized curve");
        const auto& vibrato = note.vibrato;
        if (!std::isfinite(vibrato.startPercent) || !std::isfinite(vibrato.depthCents) ||
            !std::isfinite(vibrato.rateHz) || !std::isfinite(vibrato.phase) ||
            !std::isfinite(vibrato.fadeInPercent) || !std::isfinite(vibrato.fadeOutPercent))
            errors.emplace_back("Note " + note.id + " has non-finite vibrato values");
        const auto finiteTiming = [](const std::optional<float>& value) {
            return !value || std::isfinite(*value);
        };
        if (!finiteTiming(note.phonemeTiming.preutteranceDeltaMs) ||
            !finiteTiming(note.phonemeTiming.overlapDeltaMs) ||
            !finiteTiming(note.phonemeTiming.attackTimeDeltaMs) ||
            !finiteTiming(note.phonemeTiming.releaseTimeDeltaMs))
            errors.emplace_back("Note " + note.id + " has invalid phoneme timing");
        if (previousEnd > note.startTick) errors.emplace_back("Overlapping notes are unsupported in V1");
        previousEnd = std::max(previousEnd, note.endTick());
    }
    previousEnd = -1;
    for (const auto& section : sections) {
        if (section.startTick < 0 || section.endTick <= section.startTick)
            errors.emplace_back("Section " + section.name + " has invalid bounds");
        if (previousEnd > section.startTick) errors.emplace_back("Sections overlap");
        previousEnd = std::max(previousEnd, section.endTick);
    }
    return errors;
}

void VocalScore::normalize() {
    for (auto& note : notes) {
        if (note.id.empty()) note.id = makeUuid();
        note.durationTick = std::max<int64_t>(0, note.durationTick);
        if (note.phonemeTiming.positionOffsetTick) {
            const int64_t maxOffset = note.durationTick > 0 ? note.durationTick - 1 : 0;
            note.phonemeTiming.positionOffsetTick = std::clamp(*note.phonemeTiming.positionOffsetTick,
                -note.durationTick, maxOffset);
        }
        auto clampTiming = [](std::optional<float>& value) {
            if (value && !std::isfinite(*value)) value.reset();
            else if (value) *value = std::clamp(*value, -500.f, 500.f);
        };
        clampTiming(note.phonemeTiming.preutteranceDeltaMs);
        clampTiming(note.phonemeTiming.overlapDeltaMs);
        clampTiming(note.phonemeTiming.attackTimeDeltaMs);
        clampTiming(note.phonemeTiming.releaseTimeDeltaMs);
        note.pitchCents.normalize();
        note.dynamicsDb.normalize();
        const Vibrato defaults;
        if (!std::isfinite(note.vibrato.startPercent)) note.vibrato.startPercent = defaults.startPercent;
        if (!std::isfinite(note.vibrato.depthCents)) note.vibrato.depthCents = defaults.depthCents;
        if (!std::isfinite(note.vibrato.rateHz)) note.vibrato.rateHz = defaults.rateHz;
        if (!std::isfinite(note.vibrato.phase)) note.vibrato.phase = defaults.phase;
        if (!std::isfinite(note.vibrato.fadeInPercent)) note.vibrato.fadeInPercent = defaults.fadeInPercent;
        if (!std::isfinite(note.vibrato.fadeOutPercent)) note.vibrato.fadeOutPercent = defaults.fadeOutPercent;
    }
    for (auto& section : sections) if (section.id.empty()) section.id = makeUuid();
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {
        return a.startTick < b.startTick;
    });
    std::stable_sort(sections.begin(), sections.end(), [](const auto& a, const auto& b) {
        return a.startTick < b.startTick;
    });
}

std::string makeUuid() {
    static std::atomic<uint64_t> counter{1};
    const auto n = counter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream out;
    out << "vr-" << std::hex << std::setw(16) << std::setfill('0') << n;
    return out.str();
}

VocalScore makeJapaneseFirstSoundScore() {
    VocalScore score;
    score.title = "Adachi Rei first sound";
    const char* lyrics[] = {"あ", "だ", "ち", "れ", "い", "う"};
    // Keep the factory phrase in a comfortable lower register. Adachi Rei's
    // source vowels are bright, and the former C4-G4 contour made the short
    // Japanese consonants substantially harder to understand.
    const int pitches[] = {55, 57, 59, 60, 59, 55};
    int64_t tick = 0;
    for (int i = 0; i < 6; ++i) {
        Note note;
        note.id = makeUuid();
        note.startTick = tick;
        note.durationTick = i == 4 ? 960 : 480;
        note.midiNote = pitches[i];
        note.lyric = lyrics[i];
        if (i == 2) note.pitchCents.points = {{0, -15.f}, {240, 20.f}, {480, 0.f}};
        if (i == 3) note.dynamicsDb.points = {{0, -4.f}, {240, 1.f}, {480, -1.f}};
        if (i == 4) note.vibrato = {55.f, 35.f, 5.4f, 0.f, 15.f, 20.f};
        score.notes.push_back(note);
        tick += note.durationTick;
    }
    score.sections = {
        {makeUuid(), "NAME", 0, 5 * 480 + 480},
        {makeUuid(), "DRONE", 6 * 480, 7 * 480}
    };
    score.normalize();
    return score;
}

VocalScore makeDefaultScore() {
    VocalScore score;
    score.title = "Adachi Rei English first sound";
    const char* lyrics[] = {"we", "sing", "a", "+", "star", "+"};
    const int pitches[] = {55, 57, 59, 60, 59, 55};
    const int64_t durations[] = {480, 720, 480, 480, 960, 480};
    int64_t tick = 0;
    for (size_t index = 0; index < std::size(lyrics); ++index) {
        Note note;
        note.id = makeUuid();
        note.startTick = tick;
        note.durationTick = durations[index];
        note.midiNote = pitches[index];
        note.lyric = lyrics[index];
        if (index == 2) note.pitchCents.points = {{0, -15.f}, {240, 20.f}, {480, 0.f}};
        if (index == 3) note.dynamicsDb.points = {{0, -4.f}, {240, 1.f}, {480, -1.f}};
        if (index == 4) note.vibrato = {55.f, 35.f, 5.4f, 0.f, 15.f, 20.f};
        score.notes.push_back(note);
        tick += note.durationTick;
    }
    score.sections = {
        {makeUuid(), "ENGLISH PHRASE", 0, score.notes[4].startTick},
        {makeUuid(), "LAST VOWEL", score.notes[4].startTick, tick},
    };
    score.normalize();
    return score;
}

VocalScore makeDroneScore() {
    VocalScore score;
    score.title = "Sustained う";
    Note note;
    note.id = makeUuid();
    note.startTick = 0;
    note.durationTick = 4 * kTicksPerQuarter;
    note.midiNote = 60;
    note.lyric = "う";
    note.vibrato = {50.f, 20.f, 5.5f, 0.f, 20.f, 20.f};
    score.notes.push_back(note);
    score.sections.push_back({makeUuid(), "DRONE", 0, note.durationTick});
    return score;
}

VocalScore makeTriggeredWordScore() {
    VocalScore score;
    score.title = "Triggered word: あだち";
    const char* lyrics[] = {"あ", "だ", "ち"};
    const int pitches[] = {55, 57, 59};
    const int64_t durations[] = {240, 240, 480};
    int64_t tick = 0;
    for (size_t i = 0; i < 3; ++i) {
        Note note;
        note.id = makeUuid();
        note.startTick = tick;
        note.durationTick = durations[i];
        note.midiNote = pitches[i];
        note.lyric = lyrics[i];
        score.notes.push_back(note);
        tick += durations[i];
    }
    score.sections.push_back({makeUuid(), "WORD", 0, tick});
    score.normalize();
    return score;
}

VocalScore makeLoopPhraseScore() {
    VocalScore score;
    score.title = "Looping phrase with one-beat rest";
    const char* lyrics[] = {"あ", "だ", "ち", "れ"};
    const int pitches[] = {55, 57, 59, 60};
    int64_t tick = 0;
    for (size_t i = 0; i < 4; ++i) {
        Note note;
        note.id = makeUuid();
        note.startTick = tick;
        note.durationTick = kTicksPerQuarter;
        note.midiNote = pitches[i];
        note.lyric = lyrics[i];
        score.notes.push_back(note);
        tick += note.durationTick;
    }
    score.sections.push_back({makeUuid(), "PHRASE + 1 BEAT REST", 0, tick + kTicksPerQuarter});
    score.normalize();
    return score;
}

VocalScore makeEnglishDroneScore() {
    VocalScore score = makeDroneScore();
    score.title = "Sustained English a";
    // "a" is in the built-in English lexicon and maps to a single sustained
    // vowel. The spelling "ah" would fall back to a+h and introduce an
    // unwanted consonant at the end of an instrument-style drone.
    score.notes.front().lyric = "a";
    score.sections.front().name = "ENGLISH DRONE";
    score.normalize();
    return score;
}

VocalScore makeEnglishTriggeredWordScore() {
    VocalScore score;
    score.title = "Triggered English word: sing";
    Note note;
    note.id = makeUuid();
    note.startTick = 0;
    note.durationTick = 2 * kTicksPerQuarter;
    note.midiNote = 57;
    note.lyric = "sing";
    score.notes.push_back(note);
    score.sections.push_back({makeUuid(), "ENGLISH WORD", 0, note.durationTick});
    score.normalize();
    return score;
}

VocalScore makeEnglishLoopPhraseScore() {
    VocalScore score;
    score.title = "Looping English phrase with one-beat rest";
    const char* lyrics[] = {"we", "sing", "a", "star"};
    const int pitches[] = {55, 57, 59, 57};
    int64_t tick = 0;
    for (size_t i = 0; i < std::size(lyrics); ++i) {
        Note note;
        note.id = makeUuid();
        note.startTick = tick;
        note.durationTick = kTicksPerQuarter;
        note.midiNote = pitches[i];
        note.lyric = lyrics[i];
        score.notes.push_back(note);
        tick += note.durationTick;
    }
    score.sections.push_back({makeUuid(), "ENGLISH PHRASE + 1 BEAT REST", 0,
                              tick + kTicksPerQuarter});
    score.normalize();
    return score;
}

}  // namespace vocalrack
