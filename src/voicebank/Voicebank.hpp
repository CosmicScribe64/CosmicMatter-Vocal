#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vocalrack {

struct OtoEntry {
    std::filesystem::path otoPath;
    std::filesystem::path wavPath;
    std::string wavName;
    std::string alias;
    double offsetMs = 0.0;
    double consonantMs = 0.0;
    double cutoffMs = 0.0;
    double preutterMs = 0.0;
    double overlapMs = 0.0;
    bool valid = false;
    std::string error;
};

struct PrefixSuffix {
    std::string prefix;
    std::string suffix;
};

struct CharacterMetadata {
    std::string name;
    std::filesystem::path imagePath;
    std::filesystem::path samplePath;
    std::string author;
    std::string web;
    std::string version;
};

struct VoicebankDiagnostics {
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::vector<std::string> duplicateAliases;
};

class Voicebank {
public:
    std::string stableId;
    uint64_t contentRevision = 0;
    std::filesystem::path root;
    CharacterMetadata character;
    double referencePitchHz = 0.0;
    std::vector<OtoEntry> entries;
    VoicebankDiagnostics diagnostics;

    static Voicebank load(const std::filesystem::path& root, const std::string& stableId = {});
    bool valid() const noexcept { return !entries.empty() && diagnostics.errors.empty(); }
    const OtoEntry* findAlias(const std::string& alias, int midiNote) const noexcept;
    double sourcePitchFor(const std::filesystem::path& wavPath) const;
    PrefixSuffix prefixSuffixFor(int midiNote) const noexcept;
    std::vector<std::string> aliases() const;

private:
    std::unordered_map<std::string, size_t> aliasIndex_;
    std::unordered_map<std::string, double> sourcePitchByWav_;
    std::map<int, PrefixSuffix> prefixMap_;
};

int toneNameToMidi(const std::string& name) noexcept;
std::string midiToToneName(int midi);

}  // namespace vocalrack
