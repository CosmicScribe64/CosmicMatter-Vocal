#pragma once

#include "core/VocalScore.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vocalrack {

struct UstxTrackInfo {
    int index = 0;
    std::string name;
    size_t noteCount = 0;
};

struct UstxImportReport {
    std::vector<std::string> imported;
    std::vector<std::string> ignored;
    std::vector<std::string> approximated;
    std::vector<std::string> warnings;
};

struct UstxImportResult {
    VocalScore score;
    UstxImportReport report;
};

class UstxImporter {
public:
    std::vector<UstxTrackInfo> scanTracks(const std::filesystem::path& path) const;
    UstxImportResult importTrack(const std::filesystem::path& path, int trackIndex) const;
};

}  // namespace vocalrack

