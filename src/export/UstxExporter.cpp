#include "UstxExporter.hpp"
#include "core/PitchModel.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace vocalrack {
namespace {

std::string yamlString(const std::string& input) {
    // A double-quoted YAML scalar also keeps colons, #, Japanese text and
    // leading/trailing spaces unambiguous. OpenUtau/YamlDotNet accepts the
    // standard JSON-compatible escapes used here.
    std::ostringstream out;
    out << '"';
    for (const unsigned char ch : input) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    out << '"';
    return out.str();
}

std::string number(double value, int precision = 5) {
    if (!std::isfinite(value)) throw std::invalid_argument("Cannot export a non-finite USTX number");
    if (std::abs(value - std::round(value)) < 1e-7)
        return std::to_string(static_cast<int64_t>(std::llround(value)));
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    auto result = out.str();
    while (!result.empty() && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result.empty() ? "0" : result;
}

}  // namespace

UstxExportResult exportUstx(const VocalScore& source, const UstxExportOptions& options) {
    VocalScore score = source;
    score.normalize();
    const auto validationErrors = score.validate();
    if (!validationErrors.empty()) throw std::invalid_argument("Cannot export invalid score: " + validationErrors.front());
    UstxExportResult result;
    std::ostringstream out;
    const double millisecondsPerTick = 60000.0 / (score.nominalBpm * kTicksPerQuarter);

    out << "name: " << yamlString(score.title) << '\n';
    out << "comment: " << yamlString("Exported losslessly where USTX has an equivalent; use the .vocalrack file for Rack-specific state.") << '\n';
    out << "ustx_version: 0.9\n";
    out << "resolution: " << kTicksPerQuarter << '\n';
    out << "tempos:\n- {position: 0, bpm: " << number(score.nominalBpm) << "}\n";
    out << "time_signatures:\n- {bar_position: 0, beat_per_bar: "
        << score.beatsPerBar << ", beat_unit: " << score.beatUnit << "}\n";
    out << "tracks:\n- track_name: " << yamlString(options.trackName) << '\n';
    if (!options.singer.empty()) out << "  singer: " << yamlString(options.singer) << '\n';
    if (!options.phonemizer.empty()) out << "  phonemizer: " << yamlString(options.phonemizer) << '\n';
    if (!options.renderer.empty()) {
        out << "  renderer_settings:\n";
        out << "    renderer: " << yamlString(options.renderer) << '\n';
    }
    out << "voice_parts:\n- name: " << yamlString(score.title.empty() ? "VocalRack part" : score.title) << '\n';
    out << "  track_no: 0\n  position: 0\n  duration: " << std::max<int64_t>(1, score.endTick()) << '\n';
    out << "  notes:\n";

    bool hasAlias = false;
    bool hasTiming = false;
    bool hasVibrato = false;
    bool hasPitch = false;
    bool hasNativeTiming = false;
    for (const auto& note : score.notes) {
        out << "  - position: " << note.startTick << '\n';
        out << "    duration: " << note.durationTick << '\n';
        out << "    tone: " << note.midiNote << '\n';
        out << "    lyric: " << yamlString(note.lyric) << '\n';

        const auto& timing = note.phonemeTiming;
        const bool hasIndexedOverride = std::any_of(note.phonemeOverrides.begin(),
            note.phonemeOverrides.end(), [](const std::string& alias) { return !alias.empty(); });
        const bool officialOverride = note.aliasOverride || hasIndexedOverride ||
            timing.positionOffsetTick || timing.preutteranceDeltaMs || timing.overlapDeltaMs;
        if (officialOverride) {
            hasAlias = hasAlias || note.aliasOverride.has_value() || hasIndexedOverride;
            hasTiming = true;
            out << "    phoneme_overrides:\n";
            const size_t overrideCount = std::max<size_t>(1, note.phonemeOverrides.size());
            for (size_t index = 0; index < overrideCount; ++index) {
                const std::string* alias = index < note.phonemeOverrides.size() &&
                                                   !note.phonemeOverrides[index].empty()
                                               ? &note.phonemeOverrides[index]
                                               : nullptr;
                if (index == 0 && !alias && note.aliasOverride) alias = &*note.aliasOverride;
                const bool indexHasTiming = index == 0 &&
                    (timing.positionOffsetTick || timing.preutteranceDeltaMs || timing.overlapDeltaMs);
                if (!alias && !indexHasTiming) continue;
                out << "    - {index: " << index;
                if (alias) out << ", phoneme: " << yamlString(*alias);
                if (index == 0 && timing.positionOffsetTick) out << ", offset: " << *timing.positionOffsetTick;
                if (index == 0 && timing.preutteranceDeltaMs) out << ", preutter_delta: " << number(*timing.preutteranceDeltaMs);
                if (index == 0 && timing.overlapDeltaMs) out << ", overlap_delta: " << number(*timing.overlapDeltaMs);
                out << "}\n";
            }
        }
        hasNativeTiming = hasNativeTiming || timing.attackTimeDeltaMs.has_value() ||
            timing.releaseTimeDeltaMs.has_value();

        // OpenUtau's YAML loader does not instantiate UNote.pitch when the
        // property is absent, while UNote.Validate dereferences it
        // unconditionally. Always emit a neutral pitch object for flat notes.
        out << "    pitch:\n      snap_first: "
            << (note.pitchSnapFirst ? "true" : "false") << "\n      data:\n";
        if (!note.pitchCents.points.empty()) {
            hasPitch = true;
            for (const auto& point : note.pitchCents.points) {
                out << "      - {x: " << number(point.tickOffset * millisecondsPerTick)
                    << ", y: " << number(point.value / 10.0)
                    << ", shape: l}\n";
            }
        } else {
            out << "      - {x: " << (note.pitchSnapFirst ? "-" : "")
                << number(note.pitchSnapFirst ? kDefaultPortamentoHalfMs : 0.0)
                << ", y: 0, shape: io}\n";
            out << "      - {x: " << number(kDefaultPortamentoHalfMs)
                << ", y: 0, shape: io}\n";
        }
        const auto& vibrato = note.vibrato;
        const float rate = vibrato.rateHz > 0.f ? vibrato.rateHz : 5.5f;
        const float length = vibrato.depthCents > 0.f
            ? std::clamp(100.f - vibrato.startPercent, 0.f, 100.f) : 0.f;
        hasVibrato = hasVibrato || length > 0.f;
        constexpr double twoPi = 6.28318530717958647692;
        out << "    vibrato: {length: " << number(length)
            << ", period: " << number(1000.0 / rate)
            << ", depth: " << number(vibrato.depthCents)
            << ", in: " << number(vibrato.fadeInPercent)
            << ", out: " << number(vibrato.fadeOutPercent)
            << ", shift: " << number(vibrato.phase * 100.0 / twoPi) << "}\n";
    }

    // OpenUtau's DYN curve is part-wide and measured in tenths of a dB.
    // Emit explicit zero anchors for notes without authored dynamics so a
    // previous note's value never bleeds into them.
    std::map<int64_t, int64_t> dynamics;
    for (const auto& note : score.notes) {
        if (note.dynamicsDb.points.empty()) {
            dynamics[note.startTick] = 0;
            dynamics[note.endTick()] = 0;
            continue;
        }
        dynamics[note.startTick] = static_cast<int64_t>(std::llround(note.dynamicsDb.sample(0, 0.f) * 10.f));
        for (const auto& point : note.dynamicsDb.points) {
            const int64_t offset = std::clamp<int64_t>(point.tickOffset, 0, note.durationTick);
            dynamics[note.startTick + offset] = static_cast<int64_t>(std::llround(point.value * 10.f));
        }
        dynamics[note.endTick()] = static_cast<int64_t>(std::llround(
            note.dynamicsDb.sample(note.durationTick, 0.f) * 10.f));
    }
    if (!dynamics.empty()) {
        out << "  curves:\n  - abbr: dyn\n    xs: [";
        bool first = true;
        for (const auto& item : dynamics) { if (!first) out << ", "; first = false; out << item.first; }
        out << "]\n    ys: [";
        first = true;
        for (const auto& item : dynamics) { if (!first) out << ", "; first = false; out << item.second; }
        out << "]\n";
    }

    result.text = out.str();
    result.preserved = {"notes, lyrics, timing and tone", "tempo and time signature"};
    if (hasAlias) result.preserved.emplace_back("phoneme/alias overrides");
    if (hasPitch) result.preserved.emplace_back("linear pitch curves");
    result.preserved.emplace_back("OpenUtau Standard adjacent-note portamento for notes without an explicit curve");
    if (!dynamics.empty()) result.preserved.emplace_back("dynamics curves");
    if (hasVibrato) result.preserved.emplace_back("vibrato depth, start, rate, fades and phase");
    if (hasTiming) result.preserved.emplace_back("OpenUtau phoneme offset, preutterance and overlap overrides");
    if (!score.sections.empty()) result.nativeOnly.emplace_back("Rack playback sections");
    if (hasNativeTiming) result.nativeOnly.emplace_back("VocalRack attack/release timing deltas");
    result.nativeOnly.emplace_back("Rack transport, CV routing, singer path and editor view state");
    if (dynamics.size() > 1)
        result.approximated.emplace_back("Per-note dynamics are represented by OpenUtau's continuous part DYN lane");
    return result;
}

}  // namespace vocalrack
