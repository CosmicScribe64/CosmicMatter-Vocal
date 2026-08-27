#include "Voicebank.hpp"

#include "core/Encoding.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace vocalrack {
namespace fs = std::filesystem;

static bool isContainedBy(const fs::path& root, const fs::path& candidate) {
    std::error_code ec;
    const auto canonicalRoot = fs::weakly_canonical(root, ec);
    if (ec) return false;
    const auto canonicalCandidate = fs::weakly_canonical(candidate, ec);
    if (ec) return false;
    const auto relative = fs::relative(canonicalCandidate, canonicalRoot, ec);
    if (ec || relative.is_absolute()) return false;
    for (const auto& component : relative)
        if (component == "..") return false;
    return true;
}

static uint64_t combineRevision(uint64_t seed, const fs::path& path) {
    std::error_code ec;
    const auto size = fs::is_regular_file(path, ec) ? fs::file_size(path, ec) : 0;
    ec.clear();
    const auto stamp = fs::last_write_time(path, ec);
    const auto ticks = ec ? 0 : stamp.time_since_epoch().count();
    const uint64_t value = static_cast<uint64_t>(std::hash<std::string>{}(path.lexically_normal().string())) ^
        (static_cast<uint64_t>(size) + 0x9e3779b97f4a7c15ULL) ^ static_cast<uint64_t>(ticks);
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

static std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> out;
    std::stringstream stream(s);
    for (std::string part; std::getline(stream, part, delimiter);) out.push_back(part);
    if (!s.empty() && s.back() == delimiter) out.emplace_back();
    return out;
}

static double parseNumber(const std::string& s, bool& ok) {
    if (trim(s).empty()) { ok = true; return 0.0; }
    try {
        size_t used = 0;
        const double v = std::stod(trim(s), &used);
        ok = used == trim(s).size() && std::isfinite(v);
        return v;
    } catch (...) { ok = false; return 0.0; }
}

static std::string lowerAscii(std::string s) {
    for (auto& c : s) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(c));
    return s;
}

static std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        return value.substr(1, value.size() - 2);
    return value;
}

static double readFrqAverage(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char header[20]{};
    if (!in.read(reinterpret_cast<char*>(header), sizeof(header))
        || std::memcmp(header, "FREQ0003", 8) != 0) return 0.0;
    uint64_t bits = 0;
    for (size_t i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(header[12 + i]) << (i * 8);
    double pitch = 0.0;
    std::memcpy(&pitch, &bits, sizeof(pitch));
    return std::isfinite(pitch) && pitch >= 50.0 && pitch <= 1200.0 ? pitch : 0.0;
}

int toneNameToMidi(const std::string& name) noexcept {
    if (name.size() < 2) return -1;
    const char note = static_cast<char>(std::toupper(name[0]));
    const std::string letters = "C D EF G A B";
    const auto pos = letters.find(note);
    if (pos == std::string::npos) return -1;
    int semitone = static_cast<int>(pos);
    size_t i = 1;
    if (i < name.size() && (name[i] == '#' || name[i] == 'b')) semitone += name[i++] == '#' ? 1 : -1;
    try {
        const int octave = std::stoi(name.substr(i));
        const int midi = (octave + 1) * 12 + semitone;
        return midi >= 0 && midi <= 127 ? midi : -1;
    } catch (...) { return -1; }
}

std::string midiToToneName(int midi) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    midi = std::clamp(midi, 0, 127);
    return std::string(names[midi % 12]) + std::to_string(midi / 12 - 1);
}

static void parseCharacter(const fs::path& root, CharacterMetadata& out, VoicebankDiagnostics& diagnostics) {
    const auto character = root / "character.txt";
    const auto yaml = root / "character.yaml";
    if (!fs::exists(character) && !fs::exists(yaml)) {
        diagnostics.errors.emplace_back("character.txt or character.yaml is missing");
        return;
    }
    try {
        if (fs::exists(character)) {
            std::stringstream lines(readDecodedText(character));
            for (std::string line; std::getline(lines, line);) {
                line = trim(line);
                size_t delimiter = line.find('=');
                if (delimiter == std::string::npos) delimiter = line.find(':');
                if (delimiter == std::string::npos) continue;
                auto key = lowerAscii(trim(line.substr(0, delimiter)));
                auto value = trim(line.substr(delimiter + 1));
                if (key == "name" || (key == "名前" && out.name.empty())) out.name = value;
                else if (key == "image") out.imagePath = root / value;
                else if (key == "sample") out.samplePath = root / value;
                else if (key == "author" || key == "created by") out.author = value;
                else if (key == "web") out.web = value;
                else if (key == "version") out.version = value;
            }
        }
        // OpenUtau-native banks may use character.yaml alone, while hybrid
        // banks use it to supplement the traditional character.txt.
        if (fs::exists(yaml)) {
            std::stringstream y(readDecodedText(yaml));
            for (std::string line; std::getline(y, line);) {
                const auto d = line.find(':');
                if (d == std::string::npos) continue;
                const auto key = lowerAscii(trim(line.substr(0, d)));
                const auto value = unquote(line.substr(d + 1));
                if (key == "name" && out.name.empty()) out.name = value;
                else if ((key == "portrait" || key == "image") && !value.empty() && fs::exists(root / value)) out.imagePath = root / value;
                else if (key == "sample" && out.samplePath.empty()) out.samplePath = root / value;
                else if (key == "author" && out.author.empty()) out.author = value;
                else if (key == "web" && out.web.empty()) out.web = value;
                else if (key == "version" && out.version.empty()) out.version = value;
            }
        }
        if (out.name.empty()) out.name = root.filename().string();
        if (!out.imagePath.empty() && !isContainedBy(root, out.imagePath)) {
            diagnostics.warnings.emplace_back("Character image escapes the voicebank root and was ignored");
            out.imagePath.clear();
        }
        if (!out.samplePath.empty() && !isContainedBy(root, out.samplePath)) {
            diagnostics.warnings.emplace_back("Character sample escapes the voicebank root and was ignored");
            out.samplePath.clear();
        }
        if (!out.imagePath.empty() && !fs::exists(out.imagePath)) {
            diagnostics.warnings.emplace_back("Character image is missing: " + out.imagePath.string());
            out.imagePath.clear();
        }
    } catch (const std::exception& e) {
        diagnostics.errors.emplace_back(std::string("Unable to decode singer metadata: ") + e.what());
    }
}

static void parsePrefixMap(const fs::path& root, std::map<int, PrefixSuffix>& map, VoicebankDiagnostics& diagnostics) {
    const auto path = root / "prefix.map";
    if (!fs::exists(path)) return;
    try {
        std::stringstream lines(readDecodedText(path));
        for (std::string line; std::getline(lines, line);) {
            auto fields = split(trim(line), '\t');
            if (fields.size() != 3) {
                if (!trim(line).empty()) diagnostics.warnings.emplace_back("Malformed prefix.map line: " + line);
                continue;
            }
            const int midi = toneNameToMidi(trim(fields[0]));
            if (midi >= 0) map[midi] = {trim(fields[1]), trim(fields[2])};
        }
    } catch (const std::exception& e) {
        diagnostics.errors.emplace_back(std::string("Unable to decode prefix.map: ") + e.what());
    }
}

static std::optional<OtoEntry> parseOtoLine(const fs::path& root, const fs::path& otoPath,
                                            const std::string& raw, size_t lineNumber) {
    const auto line = trim(raw);
    if (line.empty() || line.rfind("#Charset:", 0) == 0) return std::nullopt;
    OtoEntry entry;
    entry.otoPath = otoPath;
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
        entry.error = "Missing '=' at " + otoPath.string() + ":" + std::to_string(lineNumber);
        return entry;
    }
    entry.wavName = trim(line.substr(0, equals));
    auto values = split(line.substr(equals + 1), ',');
    if (values.size() < 6) {
        entry.error = "Expected six oto values at " + otoPath.string() + ":" + std::to_string(lineNumber);
        return entry;
    }
    entry.alias = trim(values[0]);
    if (entry.alias.empty()) {
        entry.alias = entry.wavName;
        const auto dot = entry.alias.find_last_of('.');
        if (dot != std::string::npos) entry.alias.resize(dot);
    }
    bool ok[5]{};
    entry.offsetMs = parseNumber(values[1], ok[0]);
    entry.consonantMs = parseNumber(values[2], ok[1]);
    entry.cutoffMs = parseNumber(values[3], ok[2]);
    entry.preutterMs = parseNumber(values[4], ok[3]);
    entry.overlapMs = parseNumber(values[5], ok[4]);
    if (!std::all_of(std::begin(ok), std::end(ok), [](bool v) { return v; })) {
        entry.error = "Malformed numeric oto value at " + otoPath.string() + ":" + std::to_string(lineNumber);
        return entry;
    }
    entry.wavPath = otoPath.parent_path() / fs::u8path(entry.wavName);
    if (!isContainedBy(root, entry.wavPath)) {
        entry.error = "Referenced WAV escapes the voicebank root: " + entry.wavPath.string();
        return entry;
    }
    if (!fs::exists(entry.wavPath)) {
        entry.error = "Referenced WAV is missing: " + entry.wavPath.string();
        return entry;
    }
    entry.valid = true;
    return entry;
}

Voicebank Voicebank::load(const fs::path& path, const std::string& id) {
    Voicebank bank;
    bank.root = fs::absolute(path);
    bank.stableId = id.empty() ? bank.root.string() : id;
    if (!fs::is_directory(bank.root)) {
        bank.diagnostics.errors.emplace_back("Voicebank folder does not exist: " + bank.root.string());
        return bank;
    }
    parseCharacter(bank.root, bank.character, bank.diagnostics);
    parsePrefixMap(bank.root, bank.prefixMap_, bank.diagnostics);

    std::vector<fs::path> otoFiles, frqFiles;
    std::error_code ec;
    constexpr size_t kMaximumScannedEntries = 50000;
    constexpr int kMaximumScanDepth = 12;
    constexpr auto kMaximumScanTime = std::chrono::seconds(2);
    const auto scanBegan = std::chrono::steady_clock::now();
    size_t scannedEntries = 0;
    for (fs::recursive_directory_iterator it(bank.root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (++scannedEntries > kMaximumScannedEntries || std::chrono::steady_clock::now() - scanBegan > kMaximumScanTime) {
            bank.diagnostics.errors.emplace_back("Voicebank scan exceeded its file-count or time budget");
            break;
        }
        if (it.depth() >= kMaximumScanDepth && it->is_directory(ec)) {
            it.disable_recursion_pending();
            ec.clear();
        }
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        const auto filename = lowerAscii(it->path().filename().string());
        if (filename == "oto.ini") otoFiles.push_back(it->path());
        else if (lowerAscii(it->path().extension().string()) == ".frq") frqFiles.push_back(it->path());
    }
    std::sort(otoFiles.begin(), otoFiles.end());
    if (otoFiles.empty()) bank.diagnostics.errors.emplace_back("No oto.ini found in voicebank");
    std::set<std::string> duplicateSet;
    for (const auto& oto : otoFiles) {
        try {
            std::stringstream lines(readDecodedText(oto));
            size_t lineNumber = 0;
            for (std::string line; std::getline(lines, line);) {
                ++lineNumber;
                auto parsed = parseOtoLine(bank.root, oto, line, lineNumber);
                if (!parsed) continue;
                if (!parsed->valid) {
                    bank.diagnostics.warnings.push_back(parsed->error);
                    continue;
                }
                const size_t index = bank.entries.size();
                bank.entries.push_back(std::move(*parsed));
                const auto insertion = bank.aliasIndex_.emplace(bank.entries.back().alias, index);
                if (!insertion.second) duplicateSet.insert(bank.entries.back().alias);
            }
        } catch (const std::exception& e) {
            bank.diagnostics.warnings.emplace_back("Unable to load " + oto.string() + ": " + e.what());
        }
    }
    bank.diagnostics.duplicateAliases.assign(duplicateSet.begin(), duplicateSet.end());
    if (!duplicateSet.empty()) bank.diagnostics.warnings.emplace_back(
        std::to_string(duplicateSet.size()) + " duplicate aliases; deterministic first oto path/line wins");
    std::vector<double> bankPitches;
    for (const auto& frq : frqFiles) {
        const double pitch = readFrqAverage(frq);
        if (pitch <= 0.0) continue;
        bankPitches.push_back(pitch);
        auto stem = frq.stem().string();
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, "_wav") == 0) {
            stem.resize(stem.size() - 4);
            bank.sourcePitchByWav_[fs::absolute(frq.parent_path() / fs::u8path(stem + ".wav")).lexically_normal().string()] = pitch;
        }
    }
    if (!bankPitches.empty()) {
        const auto middle = bankPitches.begin() + static_cast<std::ptrdiff_t>(bankPitches.size() / 2);
        std::nth_element(bankPitches.begin(), middle, bankPitches.end());
        bank.referencePitchHz = *middle;
    }
    uint64_t revision = 1469598103934665603ULL;
    for (const auto& path : otoFiles) revision = combineRevision(revision, path);
    for (const auto& path : frqFiles) revision = combineRevision(revision, path);
    for (const auto& entry : bank.entries) revision = combineRevision(revision, entry.wavPath);
    bank.contentRevision = revision;
    return bank;
}

PrefixSuffix Voicebank::prefixSuffixFor(int midiNote) const noexcept {
    const auto exact = prefixMap_.find(midiNote);
    if (exact != prefixMap_.end()) return exact->second;
    return {};
}

const OtoEntry* Voicebank::findAlias(const std::string& alias, int midiNote) const noexcept {
    const auto mapping = prefixSuffixFor(midiNote);
    const std::string mapped = mapping.prefix + alias + mapping.suffix;
    auto it = aliasIndex_.find(mapped);
    if (it == aliasIndex_.end() && mapped != alias) it = aliasIndex_.find(alias);
    return it == aliasIndex_.end() ? nullptr : &entries[it->second];
}

double Voicebank::sourcePitchFor(const fs::path& wavPath) const {
    const auto key = fs::absolute(wavPath).lexically_normal().string();
    const auto found = sourcePitchByWav_.find(key);
    return found != sourcePitchByWav_.end() ? found->second : referencePitchHz;
}

std::vector<std::string> Voicebank::aliases() const {
    std::vector<std::string> out;
    out.reserve(aliasIndex_.size());
    for (const auto& item : aliasIndex_) out.push_back(item.first);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace vocalrack
