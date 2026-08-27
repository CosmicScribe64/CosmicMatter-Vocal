#pragma once

#include "VocalScore.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace vocalrack {

// Portable, lossless interchange state for moving a VocalRack score between
// module instances. Rack patch cables remain in the .vcv patch; every setting
// owned by the Vocal module itself is represented here.
struct VocalProjectState {
    VocalScore score;
    std::string singerId = "builtin:adachi-rei";
    std::string externalSingerPath;
    std::string phonemizerName = "EN X-SAMPA";
    int ppqn = 24;
    int runRisingBehavior = 0;
    int sectionQuantization = 3;
    bool panelPlaying = false;
    bool loop = false;
    bool sectionRange = false;
    float bpm = 120.f;
    int transpose = 0;
    int section = 0;
    float pitchCvAmount = 0.f;
    float dynamicsCvAmount = 0.f;
    float vibratoCvAmount = 0.f;
    float formCvAmount = 0.f;
    float editorScrollX = 0.f;
    float editorScrollY = 60.f;
    float editorZoomX = 0.16f;
    float editorZoomY = 12.f;
    bool editorFollowPlayhead = true;
    bool editorSnapEnabled = true;
    int64_t editorSnapTick = 120;
};

std::string projectToJson(const VocalProjectState& project, bool pretty = true);
VocalProjectState projectFromJson(const std::string& text);
void saveProjectFile(const std::filesystem::path& path, const VocalProjectState& project);
VocalProjectState loadProjectFile(const std::filesystem::path& path);

}  // namespace vocalrack
