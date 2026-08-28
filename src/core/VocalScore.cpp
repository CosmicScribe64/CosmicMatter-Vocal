#include "VocalScore.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <unordered_set>

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

namespace {

void sliceCurve(Curve& curve, int64_t beginOffset, int64_t endOffset) {
    if (curve.points.empty()) return;
    const Curve authored = curve;
    const int64_t duration = endOffset - beginOffset;
    curve.points.clear();
    curve.points.push_back({0, authored.sample(beginOffset)});
    for (const auto& point : authored.points) {
        if (point.tickOffset > beginOffset && point.tickOffset < endOffset)
            curve.points.push_back({point.tickOffset - beginOffset, point.value});
    }
    curve.points.push_back({duration, authored.sample(endOffset)});
    curve.normalize();
}

}  // namespace

void resolveMonophonicOverwrite(VocalScore& score,
                                const std::vector<std::string>& priorityNoteIds,
                                int64_t minimumDurationTick) {
    if (priorityNoteIds.empty()) return;
    minimumDurationTick = std::max<int64_t>(1, minimumDurationTick);
    const std::unordered_set<std::string> priority(priorityNoteIds.begin(), priorityNoteIds.end());

    std::vector<std::pair<int64_t, int64_t>> occupied;
    for (const auto& note : score.notes) {
        if (priority.count(note.id) && note.durationTick > 0)
            occupied.emplace_back(note.startTick, note.endTick());
    }
    if (occupied.empty()) return;
    std::sort(occupied.begin(), occupied.end());
    std::vector<std::pair<int64_t, int64_t>> merged;
    for (const auto& interval : occupied) {
        if (merged.empty() || interval.first > merged.back().second)
            merged.push_back(interval);
        else
            merged.back().second = std::max(merged.back().second, interval.second);
    }

    std::vector<Note> kept;
    kept.reserve(score.notes.size());
    for (auto note : score.notes) {
        if (priority.count(note.id)) {
            kept.push_back(std::move(note));
            continue;
        }

        const int64_t originalStart = note.startTick;
        const int64_t originalEnd = note.endTick();
        std::vector<std::pair<int64_t, int64_t>> remaining{{originalStart, originalEnd}};
        for (const auto& block : merged) {
            if (block.second <= originalStart || block.first >= originalEnd) continue;
            std::vector<std::pair<int64_t, int64_t>> next;
            for (const auto& segment : remaining) {
                if (block.second <= segment.first || block.first >= segment.second) {
                    next.push_back(segment);
                    continue;
                }
                if (segment.first < block.first)
                    next.emplace_back(segment.first, std::min(segment.second, block.first));
                if (block.second < segment.second)
                    next.emplace_back(std::max(segment.first, block.second), segment.second);
            }
            remaining = std::move(next);
            if (remaining.empty()) break;
        }

        const auto survivor = std::find_if(remaining.begin(), remaining.end(), [&](const auto& segment) {
            return segment.second - segment.first >= minimumDurationTick;
        });
        if (survivor == remaining.end()) continue;

        const int64_t beginOffset = survivor->first - originalStart;
        const int64_t endOffset = survivor->second - originalStart;
        if (beginOffset != 0 || endOffset != note.durationTick) {
            sliceCurve(note.pitchCents, beginOffset, endOffset);
            sliceCurve(note.dynamicsDb, beginOffset, endOffset);
        }
        note.startTick = survivor->first;
        note.durationTick = survivor->second - survivor->first;
        kept.push_back(std::move(note));
    }
    score.notes = std::move(kept);
    score.normalize();
}

void placeEditorDrawnNote(VocalScore& score, Note note) {
    note.startTick = std::max<int64_t>(0, note.startTick);
    note.durationTick = std::max<int64_t>(1, note.durationTick);
    note.midiNote = std::clamp(note.midiNote, 0, 127);
    if (note.id.empty()) note.id = makeUuid();
    const std::string placedId = note.id;
    score.notes.erase(std::remove_if(score.notes.begin(), score.notes.end(), [&](const Note& existing) {
        return existing.id == placedId;
    }), score.notes.end());
    score.notes.push_back(std::move(note));
    resolveMonophonicOverwrite(score, {placedId});
}

void applyEditorNoteGesture(VocalScore& score, const EditorNoteGesture& gesture) {
    if (gesture.noteIds.empty()) return;
    const std::unordered_set<std::string> selected(gesture.noteIds.begin(), gesture.noteIds.end());
    const int64_t grid = std::max<int64_t>(1, gesture.snapTick);
    const int64_t deltaTick = gesture.snapEnabled
        ? static_cast<int64_t>(std::llround(static_cast<double>(gesture.rawDeltaTick) / grid)) * grid
        : gesture.rawDeltaTick;
    const int64_t minimumDuration = gesture.snapEnabled ? grid : 1;
    const auto snapAbsolute = [&](int64_t tick) {
        if (!gesture.snapEnabled) return std::max<int64_t>(0, tick);
        return std::max<int64_t>(0,
            static_cast<int64_t>(std::llround(static_cast<double>(tick) / grid)) * grid);
    };

    if (gesture.kind == EditorNoteGestureKind::Move) {
        int64_t firstStart = std::numeric_limits<int64_t>::max();
        int lowestPitch = 127;
        int highestPitch = 0;
        for (const auto& note : score.notes) {
            if (!selected.count(note.id)) continue;
            firstStart = std::min(firstStart, note.startTick);
            lowestPitch = std::min(lowestPitch, note.midiNote);
            highestPitch = std::max(highestPitch, note.midiNote);
        }
        if (firstStart == std::numeric_limits<int64_t>::max()) return;
        const int64_t clampedTick = std::max(deltaTick, -firstStart);
        const int clampedPitch = std::clamp(gesture.deltaMidi, -lowestPitch, 127 - highestPitch);
        for (auto& note : score.notes) {
            if (!selected.count(note.id)) continue;
            note.startTick += clampedTick;
            note.midiNote += clampedPitch;
        }
        resolveMonophonicOverwrite(score, gesture.noteIds);
        return;
    }

    auto edited = std::find_if(score.notes.begin(), score.notes.end(), [&](const Note& note) {
        return note.id == gesture.primaryNoteId && selected.count(note.id);
    });
    if (edited == score.notes.end()) return;
    if (gesture.kind == EditorNoteGestureKind::ResizeStart) {
        const int64_t originalEnd = edited->endTick();
        // Imported/free-timed notes can be shorter than the active snap grid.
        // Preserve at least one tick in that case instead of constructing an
        // invalid clamp range whose upper bound precedes the note start.
        const int64_t effectiveMinimum = std::min(
            minimumDuration, std::max<int64_t>(1, edited->durationTick));
        edited->startTick = std::clamp(snapAbsolute(edited->startTick + deltaTick),
                                       int64_t{0}, originalEnd - effectiveMinimum);
        edited->durationTick = originalEnd - edited->startTick;
    } else {
        edited->durationTick = std::max<int64_t>(minimumDuration,
            snapAbsolute(edited->durationTick + deltaTick));
    }
    resolveMonophonicOverwrite(score, {edited->id});
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
    score.title = "Wake up, little machine";
    const char* lyrics[] = {"wake", "up", "little", "machine"};
    const int pitches[] = {55, 57, 59, 60};
    const int64_t durations[] = {720, 480, 960, 1440};
    int64_t tick = 0;
    for (size_t index = 0; index < std::size(lyrics); ++index) {
        Note note;
        note.id = makeUuid();
        note.startTick = tick;
        note.durationTick = durations[index];
        note.midiNote = pitches[index];
        note.lyric = lyrics[index];
        if (index == 1) note.dynamicsDb.points = {{0, -4.f}, {240, 1.f}, {480, -1.f}};
        if (index == 2) note.pitchCents.points = {{0, -15.f}, {480, 20.f}, {960, 0.f}};
        if (index == 3) note.vibrato = {55.f, 35.f, 5.4f, 0.f, 15.f, 20.f};
        score.notes.push_back(note);
        tick += note.durationTick;
    }
    score.sections = {
        {makeUuid(), "WAKE UP", 0, score.notes[2].startTick},
        {makeUuid(), "LITTLE MACHINE", score.notes[2].startTick, tick},
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
