#include "ProjectFile.hpp"

#include "Serialization.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace vocalrack {
using json = nlohmann::json;

std::string projectToJson(const VocalProjectState& project, bool pretty) {
    json root = {
        {"format", "VocalRackProject"},
        {"version", 1},
        {"score", json::parse(scoreToJson(project.score))},
        {"voice", {
            {"singerId", project.singerId},
            {"externalSingerPath", project.externalSingerPath},
            {"phonemizer", project.phonemizerName}
        }},
        {"transport", {
            {"ppqn", project.ppqn},
            {"runRisingBehavior", project.runRisingBehavior},
            {"sectionQuantization", project.sectionQuantization},
            {"playing", project.panelPlaying},
            {"loop", project.loop},
            {"sectionRange", project.sectionRange},
            {"bpm", project.bpm},
            {"transpose", project.transpose},
            {"section", project.section}
        }},
        {"cvAmounts", {
            {"pitch", project.pitchCvAmount},
            {"dynamics", project.dynamicsCvAmount},
            {"vibrato", project.vibratoCvAmount},
            {"form", project.formCvAmount}
        }},
        {"editor", {
            {"scrollX", project.editorScrollX},
            {"scrollY", project.editorScrollY},
            {"zoomX", project.editorZoomX},
            {"zoomY", project.editorZoomY},
            {"followPlayhead", project.editorFollowPlayhead},
            {"snapEnabled", project.editorSnapEnabled},
            {"snapTick", project.editorSnapTick}
        }}
    };
    return root.dump(pretty ? 2 : -1);
}

VocalProjectState projectFromJson(const std::string& text) {
    const auto root = json::parse(text);
    if (root.value("format", std::string()) != "VocalRackProject")
        throw std::runtime_error("Not a VocalRack project file");
    const int version = root.value("version", 0);
    if (version != 1)
        throw std::runtime_error("Unsupported VocalRack project version " + std::to_string(version));
    if (!root.contains("score") || !root["score"].is_object())
        throw std::runtime_error("VocalRack project has no score");

    VocalProjectState project;
    project.score = scoreFromJson(root["score"].dump());
    const auto voice = root.value("voice", json::object());
    project.singerId = voice.value("singerId", std::string("builtin:adachi-rei"));
    project.externalSingerPath = voice.value("externalSingerPath", std::string());
    project.phonemizerName = voice.value("phonemizer", std::string("EN X-SAMPA"));
    const auto transport = root.value("transport", json::object());
    project.ppqn = transport.value("ppqn", 24);
    project.runRisingBehavior = transport.value("runRisingBehavior", 0);
    project.sectionQuantization = transport.value("sectionQuantization", 3);
    project.panelPlaying = transport.value("playing", false);
    project.loop = transport.value("loop", false);
    project.sectionRange = transport.value("sectionRange", false);
    project.bpm = transport.value("bpm", static_cast<float>(project.score.nominalBpm));
    project.transpose = transport.value("transpose", 0);
    project.section = transport.value("section", 0);
    const auto cv = root.value("cvAmounts", json::object());
    project.pitchCvAmount = cv.value("pitch", 0.f);
    project.dynamicsCvAmount = cv.value("dynamics", 0.f);
    project.vibratoCvAmount = cv.value("vibrato", 0.f);
    project.formCvAmount = cv.value("form", 0.f);
    const auto editor = root.value("editor", json::object());
    project.editorScrollX = editor.value("scrollX", 0.f);
    project.editorScrollY = editor.value("scrollY", 60.f);
    project.editorZoomX = editor.value("zoomX", 0.16f);
    project.editorZoomY = editor.value("zoomY", 12.f);
    project.editorFollowPlayhead = editor.value("followPlayhead", true);
    project.editorSnapEnabled = editor.value("snapEnabled", true);
    project.editorSnapTick = editor.value("snapTick", static_cast<int64_t>(120));
    const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    project.bpm = finiteOr(project.bpm, static_cast<float>(project.score.nominalBpm));
    project.pitchCvAmount = finiteOr(project.pitchCvAmount, 0.f);
    project.dynamicsCvAmount = finiteOr(project.dynamicsCvAmount, 0.f);
    project.vibratoCvAmount = finiteOr(project.vibratoCvAmount, 0.f);
    project.formCvAmount = finiteOr(project.formCvAmount, 0.f);
    project.editorScrollX = finiteOr(project.editorScrollX, 0.f);
    project.editorScrollY = finiteOr(project.editorScrollY, 60.f);
    project.editorZoomX = finiteOr(project.editorZoomX, 0.16f);
    project.editorZoomY = finiteOr(project.editorZoomY, 12.f);
    return project;
}

void saveProjectFile(const std::filesystem::path& path, const VocalProjectState& project) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create VocalRack project: " + path.string());
    output << projectToJson(project, true) << '\n';
    if (!output) throw std::runtime_error("Cannot finish writing VocalRack project: " + path.string());
}

VocalProjectState loadProjectFile(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw std::runtime_error("Cannot inspect VocalRack project: " + path.string());
    if (size > 64u * 1024u * 1024u) throw std::runtime_error("VocalRack project exceeds the 64 MiB limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open VocalRack project: " + path.string());
    std::ostringstream text;
    text << input.rdbuf();
    return projectFromJson(text.str());
}

}  // namespace vocalrack
