#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vocalrack {

constexpr int64_t kTicksPerQuarter = 480;

struct CurvePoint {
    int64_t tickOffset = 0;
    float value = 0.f;
};

struct Curve {
    std::vector<CurvePoint> points;
    float sample(int64_t tickOffset, float defaultValue = 0.f) const noexcept;
    void normalize();
};

struct Vibrato {
    float startPercent = 65.f;
    float depthCents = 0.f;
    float rateHz = 5.5f;
    float phase = 0.f;
    float fadeInPercent = 10.f;
    float fadeOutPercent = 10.f;
};

// Per-note adjustments to the timing authored by the selected voicebank's
// oto.ini entry. Empty values mean "follow the voicebank". OpenUtau exposes
// the same concepts as phoneme-position and envelope handles.
struct PhonemeTiming {
    std::optional<int64_t> positionOffsetTick;
    std::optional<float> preutteranceDeltaMs;
    std::optional<float> overlapDeltaMs;
    std::optional<float> attackTimeDeltaMs;
    std::optional<float> releaseTimeDeltaMs;
};

struct Note {
    std::string id;
    int64_t startTick = 0;
    int64_t durationTick = kTicksPerQuarter;
    int midiNote = 60;
    std::string lyric = "a";
    std::optional<std::string> aliasOverride;
    // Optional exact aliases for individual phoneme events. Empty entries
    // keep that event automatic. This mirrors indexed OpenUtau
    // phoneme_overrides and supports multi-phone words without replacing the
    // entire note with one alias.
    std::vector<std::string> phonemeOverrides;
    // OpenUtau's snap_first is independent of whether pitch points exist.
    // When enabled, the first point of an adjacent note starts at the
    // predecessor's absolute pitch; rests never snap.
    bool pitchSnapFirst = true;
    Curve pitchCents;
    Curve dynamicsDb;
    Vibrato vibrato;
    PhonemeTiming phonemeTiming;

    int64_t endTick() const noexcept {
        if (durationTick > 0 && startTick > INT64_MAX - durationTick) return INT64_MAX;
        if (durationTick < 0 && startTick < INT64_MIN - durationTick) return INT64_MIN;
        return startTick + durationTick;
    }
};

struct Section {
    std::string id;
    std::string name;
    int64_t startTick = 0;
    int64_t endTick = kTicksPerQuarter;
};

struct VocalScore {
    uint32_t schemaVersion = 2;
    std::string title = "New vocal score";
    double nominalBpm = 120.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    std::vector<Note> notes;
    std::vector<Section> sections;
    uint64_t revision = 1;

    int64_t endTick() const noexcept;
    std::vector<std::string> validate() const;
    void normalize();
    void touch() noexcept { ++revision; }
};

// Keeps the notes named by priorityNoteIds exactly where an editor gesture
// placed them and removes their time spans from every other note. A collided
// note keeps its earliest remaining contiguous span; this matches OpenUtau's
// pencil/fix-overlap convention without leaving a temporarily invalid score.
// Curves are sliced and rebased with the surviving audio span.
void resolveMonophonicOverwrite(VocalScore& score,
                                const std::vector<std::string>& priorityNoteIds,
                                int64_t minimumDurationTick = 1);

// The score editor and its automated interaction tests share this exact
// gesture operation. Keeping the translation/resize and overwrite rules here
// prevents the Rack mouse handler from drifting away from the tested model.
enum class EditorNoteGestureKind { Move, ResizeStart, ResizeEnd };

struct EditorNoteGesture {
    EditorNoteGestureKind kind = EditorNoteGestureKind::Move;
    std::vector<std::string> noteIds;
    std::string primaryNoteId;
    int64_t rawDeltaTick = 0;
    int deltaMidi = 0;
    bool snapEnabled = true;
    int64_t snapTick = 120;
};

void placeEditorDrawnNote(VocalScore& score, Note note);
void applyEditorNoteGesture(VocalScore& score, const EditorNoteGesture& gesture);

std::string makeUuid();
VocalScore makeDefaultScore();
VocalScore makeJapaneseFirstSoundScore();
VocalScore makeDroneScore();
VocalScore makeTriggeredWordScore();
VocalScore makeLoopPhraseScore();
VocalScore makeEnglishDroneScore();
VocalScore makeEnglishTriggeredWordScore();
VocalScore makeEnglishLoopPhraseScore();

}  // namespace vocalrack
