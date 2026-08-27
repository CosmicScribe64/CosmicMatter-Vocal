#pragma once

#include "core/VocalScore.hpp"

#include <string>
#include <vector>

namespace vocalrack {

struct UstxExportOptions {
    std::string trackName = "VocalRack voice";
    std::string singer = "adachi-rei";
    std::string phonemizer = "OpenUtau.Plugin.Builtin.EnXSampaPhonemizer";
    std::string renderer = "CLASSIC";
};

struct UstxExportResult {
    std::string text;
    std::vector<std::string> preserved;
    std::vector<std::string> approximated;
    std::vector<std::string> nativeOnly;
};

// Exports the score semantics understood by both VocalRack and OpenUtau.
// Rack transport/CV state and VocalRack-only timing controls belong in the
// lossless .vocalrack project format rather than being disguised as USTX.
UstxExportResult exportUstx(const VocalScore& score,
                            const UstxExportOptions& options = {});

}  // namespace vocalrack
