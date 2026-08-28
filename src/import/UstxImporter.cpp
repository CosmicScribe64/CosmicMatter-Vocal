#include "UstxImporter.hpp"

#include "core/Encoding.hpp"
#include "phonemizer/Phonemizer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace vocalrack {

static constexpr uintmax_t kMaximumImportBytes = 64u * 1024u * 1024u;
static constexpr size_t kMaximumImportLines = 1000000;
static constexpr size_t kMaximumImportedNotes = 20000;
static constexpr size_t kMaximumCurvePoints = 1000000;
static constexpr int kMaximumPhonemeOverrideIndex = 255;

struct Line { int indent = 0; std::string text; };
struct ImportedCurve { std::string abbr; std::vector<int64_t> xs, ys; };
struct Part {
    int track = 0;
    int64_t position = 0;
    std::string name;
    std::vector<Note> notes;
    std::vector<std::vector<std::string>> pitchShapes;
    std::vector<bool> pitchSnapFirst;
    std::vector<ImportedCurve> curves;
};

static std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1);
}

static void appendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7f) out.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

static std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() < 2) return s;
    if (s.front() == '\'' && s.back() == '\'') {
        std::string out;
        for (size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\'' && i + 2 < s.size() && s[i + 1] == '\'') ++i;
            out.push_back(s[i]);
        }
        return out;
    }
    if (s.front() == '"' && s.back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < s.size(); ++i) {
            char ch = s[i];
            if (ch != '\\' || i + 2 >= s.size()) { out.push_back(ch); continue; }
            ch = s[++i];
            switch (ch) {
                case '"': case '\\': case '/': out.push_back(ch); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (i + 4 >= s.size()) { out += "\\u"; break; }
                    uint32_t codepoint = 0;
                    bool valid = true;
                    for (size_t digit = 0; digit < 4; ++digit) {
                        const char hex = s[++i];
                        codepoint <<= 4;
                        if (hex >= '0' && hex <= '9') codepoint |= static_cast<uint32_t>(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') codepoint |= static_cast<uint32_t>(hex - 'a' + 10);
                        else if (hex >= 'A' && hex <= 'F') codepoint |= static_cast<uint32_t>(hex - 'A' + 10);
                        else valid = false;
                    }
                    if (valid) appendUtf8(out, codepoint);
                    break;
                }
                default: out.push_back(ch); break;
            }
        }
        return out;
    }
    return s;
}

static std::vector<Line> tokenize(const std::string& text) {
    std::vector<Line> out; std::stringstream in(text);
    for (std::string raw; std::getline(in, raw);) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        int indent = 0; while (indent < static_cast<int>(raw.size()) && raw[indent] == ' ') ++indent;
        auto body = trim(raw.substr(indent)); if (body.empty() || body[0] == '#') continue;
        out.push_back({indent, body});
        if (out.size() > kMaximumImportLines) throw std::runtime_error("Project contains too many lines");
    } return out;
}

static std::pair<std::string, std::string> keyValue(std::string text) {
    if (text.rfind("- ", 0) == 0) text = text.substr(2);
    const auto colon = text.find(':'); if (colon == std::string::npos) return {trim(text), {}};
    return {trim(text.substr(0, colon)), unquote(text.substr(colon + 1))};
}

template <typename T> static T number(const std::string& s, T fallback) {
    try {
        if constexpr (std::is_integral<T>::value) {
            const auto value = std::stoll(s);
            if (value < static_cast<long long>(std::numeric_limits<T>::min()) ||
                value > static_cast<long long>(std::numeric_limits<T>::max())) return fallback;
            return static_cast<T>(value);
        } else {
            const double value = std::stod(s);
            return std::isfinite(value) && value >= -std::numeric_limits<T>::max() &&
                value <= std::numeric_limits<T>::max() ? static_cast<T>(value) : fallback;
        }
    }
    catch (...) { return fallback; }
}

static std::map<std::string, std::string> inlineMap(std::string s) {
    std::map<std::string, std::string> out; s = trim(s);
    if (!s.empty() && s.front() == '{') s.erase(s.begin());
    if (!s.empty() && s.back() == '}') s.pop_back();
    std::vector<std::string> items;
    std::string item;
    char quote = 0;
    bool escaped = false;
    int nested = 0;
    for (const char ch : s) {
        if (escaped) { item.push_back(ch); escaped = false; continue; }
        if (quote == '"' && ch == '\\') { item.push_back(ch); escaped = true; continue; }
        if (quote) {
            item.push_back(ch);
            if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') { quote = ch; item.push_back(ch); continue; }
        if (ch == '[' || ch == '{') ++nested;
        else if ((ch == ']' || ch == '}') && nested > 0) --nested;
        if (ch == ',' && nested == 0) { items.push_back(std::move(item)); item.clear(); }
        else item.push_back(ch);
    }
    items.push_back(std::move(item));
    for (auto& entry : items) { auto kv = keyValue(trim(std::move(entry))); out[kv.first] = kv.second; }
    return out;
}

static std::vector<int64_t> numberList(std::string s) {
    std::vector<int64_t> out; s = trim(s); if (!s.empty() && s.front() == '[') s.erase(s.begin());
    if (!s.empty() && s.back() == ']') s.pop_back();
    std::stringstream in(s);
    for (std::string item; std::getline(in, item, ',');) {
        if (out.size() >= kMaximumCurvePoints) throw std::runtime_error("Curve exceeds the point budget");
        out.push_back(number<int64_t>(trim(item), 0));
    }
    return out;
}

static int64_t checkedTickAdd(int64_t left, int64_t right) {
    if ((right > 0 && left > std::numeric_limits<int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<int64_t>::min() - right))
        throw std::runtime_error("Project tick position overflows the supported range");
    return left + right;
}

static int64_t checkedTickDifference(int64_t left, int64_t right) {
    const long double value = static_cast<long double>(left) - static_cast<long double>(right);
    const auto minimum = static_cast<long double>(std::numeric_limits<int64_t>::min());
    const auto maximum = static_cast<long double>(std::numeric_limits<int64_t>::max());
    if (value < minimum || value > maximum)
        throw std::runtime_error("Project tick difference overflows the supported range");
    return static_cast<int64_t>(value);
}

enum class ImportedKind { Ustx, Ust, Midi };
struct Parsed { ImportedKind kind = ImportedKind::Ustx; std::string title = "Imported USTX"; double bpm = 120; int beats = 4, unit = 4; int tempoCount = 0, signatureCount = 0; std::vector<std::string> trackNames; std::vector<Part> parts; std::vector<std::string> ignored; std::vector<std::string> warnings; };

static std::vector<std::string> splitValues(std::string value, char separator = ',') {
    std::vector<std::string> result;
    std::stringstream input(value);
    for (std::string item; std::getline(input, item, separator);) result.push_back(trim(item));
    return result;
}

static bool isRestLyric(const std::string& lyric) {
    std::string lowered;
    lowered.reserve(lyric.size());
    for (unsigned char ch : lyric) lowered.push_back(static_cast<char>(std::tolower(ch)));
    return lowered.empty() || lowered == "r" || lowered == "rest" || lowered == "pau";
}

static Parsed parseLegacyUst(const std::filesystem::path& path, const std::string& text) {
    Parsed parsed;
    parsed.kind = ImportedKind::Ust;
    parsed.title = path.stem().string();
    parsed.trackNames = {parsed.title.empty() ? "UST vocal" : parsed.title};
    Part part;
    part.name = parsed.trackNames.front();

    std::map<std::string, std::map<std::string, std::string>> sections;
    std::vector<std::string> order;
    std::string section;
    std::stringstream input(text);
    for (std::string raw; std::getline(input, raw);) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const auto line = trim(raw);
        if (line.empty()) continue;
        if (line.size() >= 3 && line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (!sections.count(section)) order.push_back(section);
            sections[section];
            continue;
        }
        const auto equals = line.find('=');
        if (section.empty() || equals == std::string::npos) continue;
        sections[section][trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    if (!sections.count("#SETTING") && !sections.count("#VERSION"))
        throw std::runtime_error("File is not a legacy UTAU UST project (#SETTING/#VERSION missing)");

    const auto setting = sections.find("#SETTING");
    if (setting != sections.end()) {
        const auto& fields = setting->second;
        if (fields.count("ProjectName") && !fields.at("ProjectName").empty()) parsed.title = fields.at("ProjectName");
        if (fields.count("Tempo")) parsed.bpm = number<double>(fields.at("Tempo"), parsed.bpm);
        if (fields.count("VoiceDir")) parsed.ignored.push_back("UST VoiceDir (the current VocalRack singer remains active)");
        if (fields.count("CacheDir")) parsed.ignored.push_back("UST CacheDir");
        if (fields.count("Mode2")) parsed.ignored.push_back("UST Mode2 renderer switch (VocalRack Native V1 remains active)");
        if (fields.count("Flags")) parsed.ignored.push_back("UST project flags (not part of VocalRack's renderer)");
    }
    if (!(parsed.bpm > 0.0) || !std::isfinite(parsed.bpm)) parsed.bpm = 120.0;
    parsed.tempoCount = 1;

    int64_t cursor = 0;
    bool sawNote = false;
    std::set<std::string> ignored;
    const auto msToTicks = [&](double milliseconds) {
        return static_cast<int64_t>(std::llround(milliseconds * parsed.bpm * kTicksPerQuarter / 60000.0));
    };
    for (const auto& name : order) {
        if (name == "#VERSION" || name == "#SETTING" || name == "#TRACKEND" ||
            name == "#PREV" || name == "#NEXT" || name == "#INSERT" || name == "#DELETE") continue;
        if (name.empty() || name.front() != '#') continue;
        const auto& fields = sections[name];
        if (!fields.count("Length")) continue;
        const int64_t length = std::max<int64_t>(1, number<int64_t>(fields.at("Length"), kTicksPerQuarter));
        const std::string lyric = fields.count("Lyric") ? fields.at("Lyric") : "a";
        if (fields.count("Tempo")) {
            const double tempo = number<double>(fields.at("Tempo"), parsed.bpm);
            if (tempo > 0.0 && std::abs(tempo - parsed.bpm) > 1e-6) parsed.tempoCount = std::max(parsed.tempoCount, 2);
        }
        if (!isRestLyric(lyric)) {
            Note note;
            note.id = makeUuid();
            note.startTick = cursor;
            note.durationTick = length;
            note.midiNote = std::clamp(number<int>(fields.count("NoteNum") ? fields.at("NoteNum") : "60", 60), 0, 127);
            note.lyric = lyric;
            // In legacy UST, Lyric normally contains the phonemized voicebank
            // alias (for example "- あ" or "a い") rather than display text.
            if (!lyricIsExtender(lyric)) note.aliasOverride = lyric;

            if (fields.count("Intensity")) {
                const double intensity = std::clamp(number<double>(fields.at("Intensity"), 100.0), 0.0, 200.0);
                const float db = intensity <= 0.0 ? -120.f : static_cast<float>(20.0 * std::log10(intensity / 100.0));
                note.dynamicsDb.points = {{0, db}, {length, db}};
            }
            if (fields.count("VBR")) {
                const auto values = splitValues(fields.at("VBR"));
                auto at = [&](size_t index, double fallback) {
                    return index < values.size() ? number<double>(values[index], fallback) : fallback;
                };
                const double periodMs = at(1, 180.0);
                note.vibrato.startPercent = static_cast<float>(100.0 - std::clamp(at(0, 0.0), 0.0, 100.0));
                note.vibrato.rateHz = periodMs > 0.0 ? static_cast<float>(1000.0 / periodMs) : 5.5f;
                note.vibrato.depthCents = static_cast<float>(at(2, 0.0));
                note.vibrato.fadeInPercent = static_cast<float>(std::clamp(at(3, 10.0), 0.0, 100.0));
                note.vibrato.fadeOutPercent = static_cast<float>(std::clamp(at(4, 10.0), 0.0, 100.0));
                note.vibrato.phase = static_cast<float>(at(5, 0.0) * 6.28318530717958647692 / 100.0);
            }
            if (fields.count("PBS") && fields.count("PBY")) {
                auto pbs = splitValues(fields.at("PBS"), ';');
                const auto pby = splitValues(fields.at("PBY"));
                const auto pbw = fields.count("PBW") ? splitValues(fields.at("PBW")) : std::vector<std::string>{};
                double positionMs = pbs.empty() ? 0.0 : number<double>(pbs[0], 0.0);
                for (size_t i = 0; i < pby.size(); ++i) {
                    note.pitchCents.points.push_back({msToTicks(positionMs), static_cast<float>(number<double>(pby[i], 0.0) * 10.0)});
                    if (i < pbw.size()) positionMs += std::max(0.0, number<double>(pbw[i], 0.0));
                }
                note.pitchCents.normalize();
            }
            for (const char* field : {"Envelope", "PreUtterance", "VoiceOverlap", "StartPoint", "Velocity",
                                      "Modulation", "Flags", "PBM", "PitchBend", "Piches"}) {
                if (fields.count(field)) ignored.insert(std::string("UST ") + field);
            }
            if (part.notes.size() >= kMaximumImportedNotes) throw std::runtime_error("Project exceeds the note-count budget");
            part.notes.push_back(std::move(note));
            sawNote = true;
        }
        cursor += length;
    }
    if (!sawNote) throw std::runtime_error("Selected UST contains no vocal notes");
    parsed.parts.push_back(std::move(part));
    parsed.ignored.insert(parsed.ignored.end(), ignored.begin(), ignored.end());
    return parsed;
}

static uint32_t readBe32(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("Truncated MIDI file");
    return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8) | static_cast<uint32_t>(bytes[offset + 3]);
}

static uint16_t readBe16(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("Truncated MIDI file");
    return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

static uint32_t readVlq(const std::vector<uint8_t>& bytes, size_t& offset, size_t end) {
    uint32_t value = 0;
    for (int count = 0; count < 4; ++count) {
        if (offset >= end) throw std::runtime_error("Truncated MIDI variable-length value");
        const uint8_t byte = bytes[offset++];
        value = (value << 7) | (byte & 0x7f);
        if (!(byte & 0x80)) return value;
    }
    throw std::runtime_error("Invalid MIDI variable-length value");
}

static std::string midiText(const std::vector<uint8_t>& bytes, size_t offset, size_t length) {
    const std::string raw(reinterpret_cast<const char*>(bytes.data() + offset), length);
    try { return decodeText(raw); } catch (...) { return raw; }
}

static Parsed parseMidi(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 14 || std::string(reinterpret_cast<const char*>(bytes.data()), 4) != "MThd")
        throw std::runtime_error("File is not a Standard MIDI File (MThd missing)");
    const uint32_t headerLength = readBe32(bytes, 4);
    if (headerLength < 6 || 8 + headerLength > bytes.size()) throw std::runtime_error("Invalid MIDI header");
    const uint16_t format = readBe16(bytes, 8);
    const uint16_t trackCount = readBe16(bytes, 10);
    const uint16_t division = readBe16(bytes, 12);
    if (format > 1) throw std::runtime_error("MIDI format 2 is not a synchronized song and cannot be imported");
    if (division & 0x8000) throw std::runtime_error("SMPTE-time MIDI is unsupported; use PPQN timing");
    if (!division) throw std::runtime_error("MIDI PPQN division is zero");

    Parsed parsed;
    parsed.kind = ImportedKind::Midi;
    parsed.title = path.stem().string();
    const auto scaleTick = [&](uint64_t tick) {
        return static_cast<int64_t>(std::llround(tick * static_cast<double>(kTicksPerQuarter) / division));
    };
    size_t offset = 8 + headerLength;
    int importedTrack = 0;
    size_t placeholderLyrics = 0;
    size_t skippedPolyphonicTracks = 0;
    for (uint16_t sourceTrack = 0; sourceTrack < trackCount; ++sourceTrack) {
        if (offset + 8 > bytes.size() || std::string(reinterpret_cast<const char*>(bytes.data() + offset), 4) != "MTrk")
            throw std::runtime_error("Invalid or truncated MIDI track chunk");
        const size_t end = offset + 8 + readBe32(bytes, offset + 4);
        if (end > bytes.size()) throw std::runtime_error("Truncated MIDI track data");
        offset += 8;
        uint64_t tick = 0;
        uint8_t running = 0;
        std::string name = "MIDI track " + std::to_string(sourceTrack + 1);
        std::map<std::pair<int, int>, std::vector<uint64_t>> active;
        struct MidiNote { uint64_t start = 0, end = 0; int pitch = 60; };
        std::vector<MidiNote> midiNotes;
        std::multimap<uint64_t, std::string> lyrics;
        while (offset < end) {
            tick += readVlq(bytes, offset, end);
            if (offset >= end) throw std::runtime_error("Truncated MIDI event");
            uint8_t status = bytes[offset];
            if (status & 0x80) { ++offset; if (status < 0xf0) running = status; }
            else {
                if (!running) throw std::runtime_error("MIDI running status used before a channel event");
                status = running;
            }
            if (status == 0xff) {
                if (offset >= end) throw std::runtime_error("Truncated MIDI meta event");
                const uint8_t type = bytes[offset++];
                const uint32_t length = readVlq(bytes, offset, end);
                if (offset + length > end) throw std::runtime_error("Truncated MIDI meta payload");
                if (type == 0x03) name = midiText(bytes, offset, length);
                else if ((type == 0x05 || type == 0x01) && length) {
                    auto lyric = trim(midiText(bytes, offset, length));
                    while (!lyric.empty() && (lyric.front() == '/' || lyric.front() == '\\')) lyric.erase(lyric.begin());
                    if (!lyric.empty() && lyric.front() != '@') lyrics.emplace(tick, lyric);
                } else if (type == 0x51 && length == 3) {
                    const uint32_t micros = (bytes[offset] << 16) | (bytes[offset + 1] << 8) | bytes[offset + 2];
                    if (micros) { if (parsed.tempoCount++ == 0) parsed.bpm = 60000000.0 / micros; }
                } else if (type == 0x58 && length >= 2) {
                    if (parsed.signatureCount++ == 0) {
                        parsed.beats = bytes[offset];
                        parsed.unit = 1 << std::min<int>(bytes[offset + 1], 6);
                    }
                }
                offset += length;
                if (type == 0x2f) break;
                continue;
            }
            if (status == 0xf0 || status == 0xf7) {
                const uint32_t length = readVlq(bytes, offset, end);
                if (offset + length > end) throw std::runtime_error("Truncated MIDI SysEx payload");
                offset += length;
                continue;
            }
            const uint8_t kind = status & 0xf0;
            const int channel = status & 0x0f;
            const int dataLength = kind == 0xc0 || kind == 0xd0 ? 1 : 2;
            if (offset + dataLength > end) throw std::runtime_error("Truncated MIDI channel event");
            const int first = bytes[offset++];
            const int second = dataLength == 2 ? bytes[offset++] : 0;
            if (kind == 0x90 && second > 0) {
                active[{channel, first}].push_back(tick);
            } else if (kind == 0x80 || (kind == 0x90 && second == 0)) {
                auto& starts = active[{channel, first}];
                if (!starts.empty()) {
                    const uint64_t start = starts.back();
                    starts.pop_back();
                    if (tick > start) midiNotes.push_back({start, tick, first});
                }
            }
        }
        offset = end;
        if (midiNotes.empty()) continue;
        std::sort(midiNotes.begin(), midiNotes.end(), [](const auto& a, const auto& b) {
            return a.start != b.start ? a.start < b.start : a.pitch > b.pitch;
        });
        bool polyphonic = false;
        uint64_t latestEnd = midiNotes.front().end;
        for (size_t i = 1; i < midiNotes.size(); ++i) {
            if (midiNotes[i].start < latestEnd) polyphonic = true;
            latestEnd = std::max(latestEnd, midiNotes[i].end);
        }
        if (polyphonic) {
            ++skippedPolyphonicTracks;
            parsed.warnings.push_back("MIDI track '" + name +
                "' was omitted because a VOCAL score must be monophonic");
            continue;
        }
        Part part;
        part.track = importedTrack++;
        part.name = name.empty() ? "MIDI vocal" : name;
        for (const auto& midi : midiNotes) {
            Note note;
            note.id = makeUuid();
            note.startTick = scaleTick(midi.start);
            note.durationTick = std::max<int64_t>(1, scaleTick(midi.end) - note.startTick);
            note.midiNote = midi.pitch;
            auto exact = lyrics.equal_range(midi.start);
            if (exact.first != exact.second) {
                note.lyric = exact.first->second;
                note.aliasOverride.reset();
            } else { note.lyric = "a"; ++placeholderLyrics; }
            if (part.notes.size() >= kMaximumImportedNotes) throw std::runtime_error("Project exceeds the note-count budget");
            part.notes.push_back(std::move(note));
        }
        parsed.trackNames.push_back(part.name);
        parsed.parts.push_back(std::move(part));
    }
    if (parsed.parts.empty()) {
        if (skippedPolyphonicTracks)
            throw std::runtime_error("MIDI file contains no monophonic melody track; split the vocal melody from chords/drums first");
        throw std::runtime_error("MIDI file contains no completed note events");
    }
    if (placeholderLyrics)
        parsed.warnings.push_back(std::to_string(placeholderLyrics) +
            " MIDI note(s) had no lyric event; neutral EN X-SAMPA vowel a was added for editing");
    parsed.ignored.push_back("MIDI controller/program/pedal events (VocalRack imports the vocal score only)");
    return parsed;
}

static Parsed parse(const std::filesystem::path& path) {
    std::error_code sizeError;
    const auto inputBytes = std::filesystem::file_size(path, sizeError);
    if (sizeError) throw std::runtime_error("Unable to inspect " + path.string());
    if (inputBytes > kMaximumImportBytes) throw std::runtime_error("Project exceeds the 64 MiB import limit");
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Unable to read " + path.string());
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (bytes.size() >= 4 && std::string(reinterpret_cast<const char*>(bytes.data()), 4) == "MThd")
            return parseMidi(path, bytes);
    }
    const auto decoded = readDecodedText(path);
    const auto leading = trim(decoded);
    if (!leading.empty() && leading.front() == '[') return parseLegacyUst(path, decoded);
    const auto lines = tokenize(decoded); if (lines.empty()) throw std::runtime_error("UTAU/OpenUtau project is empty");
    bool signature = false; for (const auto& line : lines) if (line.text.rfind("ustx_version:", 0) == 0) signature = true;
    if (!signature) throw std::runtime_error("File is neither OpenUtau USTX nor legacy UTAU UST");
    Parsed p;
    std::set<std::string> ignoredSettings;
    for (const auto& line : lines) {
        const auto key = keyValue(line.text).first;
        if (key == "renderer" || key == "renderer_settings" || key == "rendererSettings")
            ignoredSettings.insert("OpenUtau renderer selection (VocalRack Native V1 remains active)");
        else if (key == "resampler" || key == "wavtool" || key == "flags")
            ignoredSettings.insert("OpenUtau resampler/wavtool flags (not part of VocalRack's renderer)");
        else if (key == "phonemizer" || key == "phonemizer_name" || key == "phonemizerName")
            ignoredSettings.insert("OpenUtau phonemizer selection (VocalRack re-phonemizes the imported lyrics)");
        else if (key == "singer" || key == "singer_id" || key == "singerId" || key == "voice_color")
            ignoredSettings.insert("OpenUtau singer/voice-color selection (the current VocalRack singer remains active)");
    }
    p.ignored.insert(p.ignored.end(), ignoredSettings.begin(), ignoredSettings.end());
    size_t importedNoteCount = 0;
    size_t importedCurveCount = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto kv = keyValue(lines[i].text);
        if (lines[i].indent == 0 && kv.first == "name") p.title = kv.second;
        else if (lines[i].indent == 0 && kv.first == "bpm") p.bpm = number<double>(kv.second, p.bpm);
        else if (lines[i].indent == 0 && kv.first == "beat_per_bar") p.beats = number<int>(kv.second, p.beats);
        else if (lines[i].indent == 0 && kv.first == "beat_unit") p.unit = number<int>(kv.second, p.unit);
        else if (lines[i].indent == 0 && kv.first == "tempos") {
            for (++i; i < lines.size() && (lines[i].indent > 0 || lines[i].text.rfind("- ", 0) == 0); ++i) {
                if (lines[i].text.rfind("- ", 0) == 0) ++p.tempoCount;
                auto map = inlineMap(lines[i].text.rfind("- ", 0) == 0 ? lines[i].text.substr(2) : lines[i].text);
                if (map.count("bpm") && p.tempoCount <= 1) p.bpm = number<double>(map["bpm"], p.bpm);
            } --i;
        } else if (lines[i].indent == 0 && kv.first == "time_signatures") {
            for (++i; i < lines.size() && (lines[i].indent > 0 || lines[i].text.rfind("- ", 0) == 0); ++i) {
                if (lines[i].text.rfind("- ", 0) == 0) ++p.signatureCount;
                auto map = inlineMap(lines[i].text.rfind("- ", 0) == 0 ? lines[i].text.substr(2) : lines[i].text);
                if (p.signatureCount <= 1 && map.count("beat_per_bar")) p.beats = number<int>(map["beat_per_bar"], p.beats);
                if (p.signatureCount <= 1 && map.count("beat_unit")) p.unit = number<int>(map["beat_unit"], p.unit);
            } --i;
        } else if (lines[i].indent == 0 && kv.first == "tracks") {
            int current = -1;
            for (++i; i < lines.size() && (lines[i].indent > 0 || lines[i].text.rfind("- ", 0) == 0); ++i) {
                if (lines[i].text.rfind("- ", 0) == 0) { ++current; p.trackNames.push_back("Track " + std::to_string(current + 1)); }
                const auto tk = keyValue(lines[i].text);
                if (current >= 0 && (tk.first == "track_name" || tk.first == "TrackName")) p.trackNames[current] = tk.second;
            } --i;
        } else if (lines[i].indent == 0 && (kv.first == "voice_parts" || kv.first == "voiceParts")) {
            Part* part = nullptr;
            Note* note = nullptr;
            ImportedCurve* curve = nullptr;
            bool inNotes = false, inPitch = false, inCurves = false, inPhonemeOverrides = false;
            int partIndent = i + 1 < lines.size() ? lines[i + 1].indent : 0;
            int notesIndent = -1, pitchIndent = -1, curvesIndent = -1, phonemeOverridesIndent = -1;
            for (++i; i < lines.size() && (lines[i].indent > partIndent || lines[i].text.rfind("- ", 0) == 0); ++i) {
                const auto& line = lines[i]; const auto item = keyValue(line.text);
                if (line.indent == partIndent && line.text.rfind("- ", 0) == 0) {
                    p.parts.emplace_back(); part = &p.parts.back(); note = nullptr; curve = nullptr;
                    inNotes = inPitch = inCurves = inPhonemeOverrides = false;
                    if (item.first == "name") part->name = item.second;
                    continue;
                }
                if (!part) continue;
                const bool listItem = line.text.rfind("- ", 0) == 0;
                if (!listItem && line.indent == partIndent + 2 && item.first == "track_no") part->track = number<int>(item.second, 0);
                else if (!listItem && line.indent == partIndent + 2 && item.first == "position") part->position = number<int64_t>(item.second, 0);
                else if (!listItem && line.indent == partIndent + 2 && item.first == "name") part->name = item.second;
                else if (line.indent == partIndent + 2 && item.first == "notes") {
                    inNotes = true; inCurves = false; notesIndent = line.indent; note = nullptr;
                }
                else if (line.indent == partIndent + 2 && item.first == "curves") {
                    inNotes = false; inCurves = true; curvesIndent = line.indent; note = nullptr;
                }
                else if (inCurves && line.indent == curvesIndent && line.text.rfind("- ", 0) == 0) {
                    if (++importedCurveCount > kMaximumCurvePoints) throw std::runtime_error("Project exceeds the curve-count budget");
                    part->curves.emplace_back(); curve = &part->curves.back();
                    if (item.first == "abbr") curve->abbr = item.second;
                }
                else if (inCurves && curve && line.indent >= curvesIndent + 2) {
                    if (item.first == "abbr") curve->abbr = item.second;
                    else if (item.first == "xs") curve->xs = numberList(item.second);
                    else if (item.first == "ys") curve->ys = numberList(item.second);
                }
                else if (inNotes && line.indent == notesIndent && line.text.rfind("- ", 0) == 0) {
                    if (++importedNoteCount > kMaximumImportedNotes) throw std::runtime_error("Project exceeds the note-count budget");
                    part->notes.emplace_back();
                    part->pitchShapes.emplace_back();
                    part->pitchSnapFirst.push_back(true);
                    note = &part->notes.back(); note->id = makeUuid(); inPitch = inPhonemeOverrides = false;
                    if (item.first == "position") note->startTick = checkedTickAdd(part->position, number<int64_t>(item.second, 0));
                } else if (note && line.indent >= notesIndent + 2) {
                    if (inPhonemeOverrides &&
                        (line.indent < phonemeOverridesIndent ||
                         (line.indent == phonemeOverridesIndent && line.text.rfind("- ", 0) != 0)) &&
                        item.first != "phoneme_overrides" && item.first != "phonemeOverrides")
                        inPhonemeOverrides = false;
                    if (item.first == "position") note->startTick = checkedTickAdd(part->position, number<int64_t>(item.second, 0));
                    else if (item.first == "duration") note->durationTick = number<int64_t>(item.second, 0);
                    else if (item.first == "tone") note->midiNote = number<int>(item.second, 60);
                    else if (item.first == "lyric") note->lyric = item.second;
                    else if (item.first == "phonetic_hint" || item.first == "phoneticHint") note->aliasOverride = item.second;
                    else if (item.first == "pitch") { inPitch = true; inPhonemeOverrides = false; pitchIndent = line.indent; }
                    else if (item.first == "phoneme_overrides" || item.first == "phonemeOverrides") {
                        inPhonemeOverrides = true; inPitch = false; phonemeOverridesIndent = line.indent;
                    }
                    else if (inPhonemeOverrides && line.indent >= phonemeOverridesIndent &&
                             line.text.rfind("- {", 0) == 0) {
                        const auto m = inlineMap(line.text.substr(2));
                        const int index = number<int>(m.count("index") ? m.at("index") : "0", 0);
                        if (index < 0) {
                            p.ignored.push_back("USTX phoneme override with negative index " + std::to_string(index));
                        } else if (index > kMaximumPhonemeOverrideIndex) {
                            throw std::runtime_error("Phoneme override index exceeds the supported limit of 255");
                        } else {
                            auto value = [&](const char* snake, const char* camel) -> std::optional<float> {
                                if (m.count(snake)) return number<float>(m.at(snake), 0.f);
                                if (m.count(camel)) return number<float>(m.at(camel), 0.f);
                                return std::nullopt;
                            };
                            if (m.count("phoneme") && !m.at("phoneme").empty()) {
                                note->phonemeOverrides.resize(std::max<size_t>(
                                    note->phonemeOverrides.size(), static_cast<size_t>(index) + 1));
                                note->phonemeOverrides[static_cast<size_t>(index)] = m.at("phoneme");
                            }
                            if (index == 0) {
                                if (m.count("offset")) note->phonemeTiming.positionOffsetTick = number<int64_t>(m.at("offset"), 0);
                                note->phonemeTiming.preutteranceDeltaMs = value("preutter_delta", "preutterDelta");
                                note->phonemeTiming.overlapDeltaMs = value("overlap_delta", "overlapDelta");
                                note->phonemeTiming.attackTimeDeltaMs = value("attack_time_delta", "attackTimeDelta");
                                note->phonemeTiming.releaseTimeDeltaMs = value("release_time_delta", "releaseTimeDelta");
                            } else {
                                if (m.count("offset")) {
                                    note->phonemeTiming.internalPositionOffsetTicks.resize(std::max<size_t>(
                                        note->phonemeTiming.internalPositionOffsetTicks.size(),
                                        static_cast<size_t>(index)));
                                    note->phonemeTiming.internalPositionOffsetTicks[static_cast<size_t>(index) - 1] =
                                        number<int64_t>(m.at("offset"), 0);
                                }
                                if (value("preutter_delta", "preutterDelta") ||
                                       value("overlap_delta", "overlapDelta") ||
                                       value("attack_time_delta", "attackTimeDelta") ||
                                       value("release_time_delta", "releaseTimeDelta")) {
                                    p.ignored.push_back("USTX envelope fields for phoneme index " +
                                        std::to_string(index) +
                                        " (its exact alias and position offset are preserved)");
                                }
                            }
                        }
                    }
                    else if (inPitch && (item.first == "snap_first" || item.first == "snapFirst"))
                        part->pitchSnapFirst.back() = item.second != "false" && item.second != "False";
                    else if (inPitch && item.first == "- {x") {
                        // Kept for defensive compatibility; regular inline maps are handled below.
                    } else if (inPitch && line.indent > pitchIndent && line.text.rfind("- {", 0) == 0) {
                        const auto m = inlineMap(line.text.substr(2));
                        const double ms = number<double>(m.count("x") ? m.at("x") : "0", 0);
                        const float cents = number<float>(m.count("y") ? m.at("y") : "0", 0) * 10.f;
                        if (note->pitchCents.points.size() >= kMaximumCurvePoints)
                            throw std::runtime_error("Pitch curve exceeds the point budget");
                        const long double pitchTick = static_cast<long double>(ms) * p.bpm * kTicksPerQuarter / 60000.0L;
                        const auto minimumTick = static_cast<long double>(std::numeric_limits<int64_t>::min());
                        const auto maximumTick = static_cast<long double>(std::numeric_limits<int64_t>::max());
                        if (!std::isfinite(static_cast<double>(pitchTick)) ||
                            pitchTick < minimumTick || pitchTick > maximumTick)
                            throw std::runtime_error("Pitch point position is outside the supported tick range");
                        note->pitchCents.points.push_back({static_cast<int64_t>(std::llround(pitchTick)), cents});
                        part->pitchShapes.back().push_back(m.count("shape") ? m.at("shape") : "io");
                    } else if (item.first == "vibrato" && !item.second.empty()) {
                        const auto m = inlineMap(item.second);
                        const float length = number<float>(m.count("length") ? m.at("length") : "0", 0);
                        note->vibrato.startPercent = 100.f - length;
                        note->vibrato.depthCents = number<float>(m.count("depth") ? m.at("depth") : "0", 0);
                        const float period = number<float>(m.count("period") ? m.at("period") : "175", 175);
                        note->vibrato.rateHz = period > 0 ? 1000.f / period : 5.5f;
                        note->vibrato.fadeInPercent = number<float>(m.count("in") ? m.at("in") : "10", 10);
                        note->vibrato.fadeOutPercent = number<float>(m.count("out") ? m.at("out") : "10", 10);
                        constexpr float twoPi = 6.28318530717958647692f;
                        note->vibrato.phase = number<float>(m.count("shift") ? m.at("shift") : "0", 0)
                            * twoPi / 100.f;
                    }
                }
                if (note && line.text.find("unsupported") != std::string::npos) p.ignored.push_back(line.text);
            }
            --i;
        }
    }
    if (!std::isfinite(p.bpm) || p.bpm < 20.0 || p.bpm > 400.0 ||
        p.beats < 1 || p.beats > 32 || p.unit < 1 || p.unit > 32)
        throw std::runtime_error("Project tempo or time signature is outside supported limits");

    size_t materializedCurvePoints = 0;
    for (const auto& part : p.parts) for (const auto& note : part.notes) {
        const size_t notePoints = note.pitchCents.points.size() + note.dynamicsDb.points.size();
        if (notePoints < note.pitchCents.points.size() || notePoints > kMaximumCurvePoints - materializedCurvePoints)
            throw std::runtime_error("Project exceeds the total curve-point budget");
        materializedCurvePoints += notePoints;
    }
    // OpenUtau's snap-first validation rewrites the first authored pitch point
    // to the preceding adjacent note before its renderer samples the curve.
    // Preserve that portamento here. Curve stores linear points, so sample the
    // authored easing at the same 5 ms cadence OpenUtau uses internally.
    const auto eased = [](double alpha, const std::string& shape) {
        constexpr double pi = 3.14159265358979323846;
        alpha = std::clamp(alpha, 0.0, 1.0);
        if (shape == "i") return 1.0 - std::cos(alpha * pi * 0.5);
        if (shape == "o") return std::sin(alpha * pi * 0.5);
        if (shape == "l") return alpha;
        return (1.0 - std::cos(alpha * pi)) * 0.5;
    };
    const int64_t pitchStepTick = std::max<int64_t>(1, static_cast<int64_t>(std::llround(
        0.005 * p.bpm * kTicksPerQuarter / 60.0)));
    for (auto& part : p.parts) {
        for (size_t noteIndex = 0; noteIndex < part.notes.size(); ++noteIndex) {
            auto& note = part.notes[noteIndex];
            note.pitchSnapFirst = noteIndex < part.pitchSnapFirst.size()
                ? part.pitchSnapFirst[noteIndex] : true;
            auto& points = note.pitchCents.points;
            if (points.empty()) continue;
            if (part.pitchSnapFirst[noteIndex]) {
                points.front().value = noteIndex > 0 && part.notes[noteIndex - 1].endTick() == note.startTick
                    ? static_cast<float>((part.notes[noteIndex - 1].midiNote - note.midiNote) * 100)
                    : 0.f;
            }
            if (points.size() < 2) continue;
            std::vector<CurvePoint> sampled;
            for (size_t pointIndex = 0; pointIndex + 1 < points.size(); ++pointIndex) {
                const auto& a = points[pointIndex];
                const auto& b = points[pointIndex + 1];
                sampled.push_back(a);
                const auto& shape = pointIndex < part.pitchShapes[noteIndex].size()
                    ? part.pitchShapes[noteIndex][pointIndex] : std::string("io");
                size_t generatedSteps = 0;
                if (b.tickOffset > a.tickOffset) {
                    const long double span = static_cast<long double>(b.tickOffset) - static_cast<long double>(a.tickOffset);
                    const long double generated = std::ceil(span /
                        static_cast<long double>(pitchStepTick));
                    if (generated > static_cast<long double>(kMaximumCurvePoints) ||
                        sampled.size() + static_cast<size_t>(generated) > kMaximumCurvePoints)
                        throw std::runtime_error("Pitch easing exceeds the generated-point budget");
                    generatedSteps = static_cast<size_t>(generated);
                }
                for (size_t step = 1; step < generatedSteps; ++step) {
                    const int64_t tick = static_cast<int64_t>(static_cast<long double>(a.tickOffset) +
                        static_cast<long double>(step) * pitchStepTick);
                    const double alpha = static_cast<double>(
                        (static_cast<long double>(tick) - a.tickOffset) /
                        (static_cast<long double>(b.tickOffset) - a.tickOffset));
                    sampled.push_back({tick, static_cast<float>(a.value + (b.value - a.value) * eased(alpha, shape))});
                }
            }
            sampled.push_back(points.back());
            if (sampled.size() > kMaximumCurvePoints - (materializedCurvePoints - points.size()))
                throw std::runtime_error("Project exceeds the total generated curve-point budget");
            materializedCurvePoints = materializedCurvePoints - points.size() + sampled.size();
            points = std::move(sampled);
        }
    }
    // Part curves are absolute within the voice part and OpenUtau linearly
    // interpolates across note boundaries. Merge them with each note's local
    // curve at the union of their knots; copying only points physically inside
    // a note loses ramps whose endpoints straddle that note.
    const auto sampleImported = [](const ImportedCurve& curve, int64_t tick) {
        const size_t count = std::min(curve.xs.size(), curve.ys.size());
        if (count == 0 || tick < curve.xs.front() || tick > curve.xs[count - 1]) return 0.f;
        const auto upper = std::upper_bound(curve.xs.begin(), curve.xs.begin() + count, tick);
        if (upper == curve.xs.begin()) return static_cast<float>(curve.ys.front());
        if (upper == curve.xs.begin() + count) return static_cast<float>(curve.ys[count - 1]);
        const size_t right = static_cast<size_t>(upper - curve.xs.begin());
        const size_t left = right - 1;
        const double span = static_cast<double>(curve.xs[right] - curve.xs[left]);
        const double alpha = span > 0.0 ? (tick - curve.xs[left]) / span : 0.0;
        return static_cast<float>(curve.ys[left] + (curve.ys[right] - curve.ys[left]) * alpha);
    };
    for (auto& part : p.parts) {
        for (const auto& importedCurve : part.curves) {
            if (importedCurve.abbr != "pitd" && importedCurve.abbr != "dyn") {
                if (!importedCurve.abbr.empty()) p.ignored.push_back("USTX curve '" + importedCurve.abbr + "'");
                continue;
            }
            const size_t count = std::min(importedCurve.xs.size(), importedCurve.ys.size());
            if (count == 0) continue;
            for (auto& note : part.notes) {
                const int64_t localStart = checkedTickDifference(note.startTick, part.position);
                std::vector<int64_t> knots{0, note.durationTick};
                const auto& existing = importedCurve.abbr == "pitd"
                    ? note.pitchCents.points : note.dynamicsDb.points;
                for (const auto& point : existing) knots.push_back(point.tickOffset);
                for (size_t point = 0; point < count; ++point) {
                    const int64_t offset = checkedTickDifference(importedCurve.xs[point], localStart);
                    if (offset >= 0 && offset <= note.durationTick) knots.push_back(offset);
                }
                std::sort(knots.begin(), knots.end());
                knots.erase(std::unique(knots.begin(), knots.end()), knots.end());
                Curve merged;
                for (const int64_t offset : knots) {
                    if (materializedCurvePoints >= kMaximumCurvePoints)
                        throw std::runtime_error("Project exceeds the total materialized curve-point budget");
                    const float raw = sampleImported(importedCurve, checkedTickAdd(localStart, offset));
                    if (importedCurve.abbr == "pitd") {
                        merged.points.push_back({offset, note.pitchCents.sample(offset, 0.f) + raw});
                    } else {
                        const float db = raw <= -240.f ? -120.f : raw / 10.f;
                        merged.points.push_back({offset, db});
                    }
                    ++materializedCurvePoints;
                }
                if (importedCurve.abbr == "pitd") {
                    materializedCurvePoints -= note.pitchCents.points.size();
                    note.pitchCents = std::move(merged);
                } else {
                    materializedCurvePoints -= note.dynamicsDb.points.size();
                    note.dynamicsDb = std::move(merged);
                }
            }
        }
    }
    return p;
}

std::vector<UstxTrackInfo> UstxImporter::scanTracks(const std::filesystem::path& path) const {
    const auto parsed = parse(path); std::vector<UstxTrackInfo> out;
    const size_t count = std::max<size_t>(parsed.trackNames.size(), 1);
    for (size_t i = 0; i < count; ++i) {
        size_t notes = 0; for (const auto& part : parsed.parts) if (part.track == static_cast<int>(i)) notes += part.notes.size();
        if (notes) out.push_back({static_cast<int>(i), i < parsed.trackNames.size() ? parsed.trackNames[i] : "Track 1", notes});
    } return out;
}

UstxImportResult UstxImporter::importTrack(const std::filesystem::path& path, int trackIndex) const {
    const auto parsed = parse(path); if (trackIndex < 0) throw std::runtime_error("Invalid USTX track index");
    UstxImportResult result; result.score.title = parsed.title; result.score.nominalBpm = parsed.bpm;
    result.score.beatsPerBar = parsed.beats; result.score.beatUnit = parsed.unit;
    for (const auto& part : parsed.parts) if (part.track == trackIndex)
        result.score.notes.insert(result.score.notes.end(), part.notes.begin(), part.notes.end());
    if (result.score.notes.empty()) throw std::runtime_error("Selected USTX track contains no vocal notes");
    result.score.normalize();
    const auto errors = result.score.validate(); if (!errors.empty()) throw std::runtime_error("Imported vocal track is invalid: " + errors.front());
    result.report.imported = {std::to_string(result.score.notes.size()) + " notes", "nominal tempo",
        "time signature", "lyrics"};
    if (parsed.kind == ImportedKind::Ustx)
        result.report.imported.insert(result.report.imported.end(), {"safe phonetic hints", "phoneme timing overrides",
            "pitch points", "dynamics curves", "vibrato"});
    else if (parsed.kind == ImportedKind::Ust)
        result.report.imported.insert(result.report.imported.end(), {"UST aliases", "Mode2 pitch points", "intensity", "vibrato"});
    else result.report.imported.emplace_back("MIDI melody timing and pitch");
    if (parsed.tempoCount > 1) result.report.warnings.emplace_back("Tempo-map playback is unsupported in V1; first valid tempo retained");
    if (parsed.signatureCount > 1) result.report.warnings.emplace_back("Time-signature changes are unsupported in V1; first signature retained");
    for (const auto& ignored : parsed.ignored) result.report.ignored.push_back("Unsupported expression: " + ignored);
    result.report.warnings.insert(result.report.warnings.end(), parsed.warnings.begin(), parsed.warnings.end());
    if (parsed.kind == ImportedKind::Ustx)
        result.report.approximated.emplace_back(
            "USTX pitch point millisecond offsets and easing sampled at OpenUtau's 5 ms cadence at nominal tempo");
    else if (parsed.kind == ImportedKind::Ust)
        result.report.approximated.emplace_back("Legacy UST Mode2 pitch timing converted from milliseconds at nominal tempo");
    return result;
}

}  // namespace vocalrack
