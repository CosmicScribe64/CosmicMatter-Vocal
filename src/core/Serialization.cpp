#include "Serialization.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace vocalrack {
using json = nlohmann::json;

static json curveJson(const Curve& curve) {
    json out = json::array();
    for (const auto& p : curve.points) out.push_back({p.tickOffset, p.value});
    return out;
}

static Curve parseCurve(const json& j) {
    Curve curve;
    if (!j.is_array()) return curve;
    if (j.size() > 1000000) throw std::runtime_error("Curve exceeds the point-count limit");
    for (const auto& p : j) {
        if (p.is_array() && p.size() >= 2) curve.points.push_back({p[0].get<int64_t>(), p[1].get<float>()});
        else if (p.is_object()) curve.points.push_back({p.value("tickOffset", 0LL), p.value("value", 0.f)});
    }
    curve.normalize();
    return curve;
}

std::string scoreToJson(const VocalScore& score, bool pretty) {
    json root = {
        {"schemaVersion", 2}, {"title", score.title}, {"nominalBpm", score.nominalBpm},
        {"beatsPerBar", score.beatsPerBar}, {"beatUnit", score.beatUnit},
        {"revision", score.revision}, {"notes", json::array()}, {"sections", json::array()}
    };
    for (const auto& note : score.notes) {
        json n = {
            {"id", note.id}, {"startTick", note.startTick}, {"durationTick", note.durationTick},
            {"midiNote", note.midiNote}, {"lyric", note.lyric},
            {"pitchSnapFirst", note.pitchSnapFirst},
            {"pitchCents", curveJson(note.pitchCents)}, {"dynamicsDb", curveJson(note.dynamicsDb)},
            {"vibrato", {{"startPercent", note.vibrato.startPercent}, {"depthCents", note.vibrato.depthCents},
                {"rateHz", note.vibrato.rateHz}, {"phase", note.vibrato.phase},
                {"fadeInPercent", note.vibrato.fadeInPercent}, {"fadeOutPercent", note.vibrato.fadeOutPercent}}}
        };
        if (note.aliasOverride) n["aliasOverride"] = *note.aliasOverride;
        if (!note.phonemeOverrides.empty()) n["phonemeOverrides"] = note.phonemeOverrides;
        json timing = json::object();
        if (note.phonemeTiming.positionOffsetTick) timing["positionOffsetTick"] = *note.phonemeTiming.positionOffsetTick;
        if (note.phonemeTiming.preutteranceDeltaMs) timing["preutteranceDeltaMs"] = *note.phonemeTiming.preutteranceDeltaMs;
        if (note.phonemeTiming.overlapDeltaMs) timing["overlapDeltaMs"] = *note.phonemeTiming.overlapDeltaMs;
        if (note.phonemeTiming.attackTimeDeltaMs) timing["attackTimeDeltaMs"] = *note.phonemeTiming.attackTimeDeltaMs;
        if (note.phonemeTiming.releaseTimeDeltaMs) timing["releaseTimeDeltaMs"] = *note.phonemeTiming.releaseTimeDeltaMs;
        if (!timing.empty()) n["phonemeTiming"] = std::move(timing);
        root["notes"].push_back(std::move(n));
    }
    for (const auto& section : score.sections) root["sections"].push_back({
        {"id", section.id}, {"name", section.name}, {"startTick", section.startTick}, {"endTick", section.endTick}
    });
    return root.dump(pretty ? 2 : -1);
}

VocalScore scoreFromJson(const std::string& jsonText, std::string* migrationNote) {
    if (jsonText.size() > 64u * 1024u * 1024u) throw std::runtime_error("Score JSON exceeds the 64 MiB limit");
    const auto root = json::parse(jsonText);
    const int schema = root.value("schemaVersion", 1);
    if (schema > 2 || schema < 1) throw std::runtime_error("Unsupported VocalScore schema version " + std::to_string(schema));
    VocalScore score;
    score.schemaVersion = 2;
    score.title = root.value("title", std::string("Imported score"));
    score.nominalBpm = root.value("nominalBpm", root.value("bpm", 120.0));
    score.beatsPerBar = root.value("beatsPerBar", 4);
    score.beatUnit = root.value("beatUnit", 4);
    score.revision = root.value("revision", 1ULL);
    const auto notes = root.value("notes", json::array());
    const auto sections = root.value("sections", json::array());
    if (!notes.is_array() || notes.size() > 20000) throw std::runtime_error("Score exceeds the note-count limit");
    if (!sections.is_array() || sections.size() > 4096) throw std::runtime_error("Score exceeds the section-count limit");
    for (const auto& n : notes) {
        Note note;
        note.id = n.value("id", makeUuid());
        note.startTick = n.value("startTick", n.value("position", 0LL));
        note.durationTick = n.value("durationTick", n.value("duration", 0LL));
        note.midiNote = n.value("midiNote", n.value("tone", 60));
        note.lyric = n.value("lyric", std::string("a"));
        if (n.contains("aliasOverride") && n["aliasOverride"].is_string()) note.aliasOverride = n["aliasOverride"].get<std::string>();
        if (n.contains("phonemeOverrides") && n["phonemeOverrides"].is_array()) {
            if (n["phonemeOverrides"].size() > 256) throw std::runtime_error("Note exceeds the phoneme-override limit");
            note.phonemeOverrides = n["phonemeOverrides"].get<std::vector<std::string>>();
        }
        note.pitchSnapFirst = n.value("pitchSnapFirst", true);
        note.pitchCents = parseCurve(n.value("pitchCents", json::array()));
        note.dynamicsDb = parseCurve(n.value("dynamicsDb", json::array()));
        const auto v = n.value("vibrato", json::object());
        note.vibrato.startPercent = v.value("startPercent", 65.f);
        note.vibrato.depthCents = v.value("depthCents", 0.f);
        note.vibrato.rateHz = v.value("rateHz", 5.5f);
        note.vibrato.phase = v.value("phase", 0.f);
        note.vibrato.fadeInPercent = v.value("fadeInPercent", 10.f);
        note.vibrato.fadeOutPercent = v.value("fadeOutPercent", 10.f);
        const auto timing = n.value("phonemeTiming", json::object());
        if (timing.contains("positionOffsetTick") && timing["positionOffsetTick"].is_number_integer())
            note.phonemeTiming.positionOffsetTick = timing["positionOffsetTick"].get<int64_t>();
        if (timing.contains("preutteranceDeltaMs") && timing["preutteranceDeltaMs"].is_number())
            note.phonemeTiming.preutteranceDeltaMs = timing["preutteranceDeltaMs"].get<float>();
        if (timing.contains("overlapDeltaMs") && timing["overlapDeltaMs"].is_number())
            note.phonemeTiming.overlapDeltaMs = timing["overlapDeltaMs"].get<float>();
        if (timing.contains("attackTimeDeltaMs") && timing["attackTimeDeltaMs"].is_number())
            note.phonemeTiming.attackTimeDeltaMs = timing["attackTimeDeltaMs"].get<float>();
        if (timing.contains("releaseTimeDeltaMs") && timing["releaseTimeDeltaMs"].is_number())
            note.phonemeTiming.releaseTimeDeltaMs = timing["releaseTimeDeltaMs"].get<float>();
        score.notes.push_back(std::move(note));
    }
    for (const auto& s : sections) score.sections.push_back({
        s.value("id", makeUuid()), s.value("name", std::string("SECTION")),
        s.value("startTick", 0LL), s.value("endTick", 0LL)
    });
    score.normalize();
    const auto errors = score.validate();
    if (!errors.empty()) throw std::runtime_error("Invalid score: " + errors.front());
    if (schema < 2 && migrationNote) *migrationNote = "Migrated VocalScore schema 1 to schema 2";
    return score;
}

}  // namespace vocalrack
