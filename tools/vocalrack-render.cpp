#include "core/Serialization.hpp"
#include "core/ProjectFile.hpp"
#include "export/UstxExporter.hpp"
#include "import/UstxImporter.hpp"
#include "render/NativeV1Renderer.hpp"
#include "render/Wav.hpp"
#include "voicebank/Voicebank.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace vocalrack;

static std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to read score " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    try {
        std::filesystem::path singerPath, scorePath, projectPath, nativeProjectPath, outputPath, exportUstxPath;
        std::string exportSingerId;
        int trackIndex = 0;
        int soloNote = -1;
        bool inspectOnly = false;
        RenderOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing value after " + arg); return argv[i]; };
            if (arg == "--singer") singerPath = value();
            else if (arg == "--score") scorePath = value();
            else if (arg == "--ustx" || arg == "--ust" || arg == "--midi" || arg == "--project") projectPath = value();
            else if (arg == "--vocalrack") nativeProjectPath = value();
            else if (arg == "--export-ustx") exportUstxPath = value();
            else if (arg == "--export-singer-id") exportSingerId = value();
            else if (arg == "--track") trackIndex = std::stoi(value());
            else if (arg == "--solo-note") soloNote = std::stoi(value());
            else if (arg == "--out") outputPath = value();
            else if (arg == "--bpm") options.bpm = std::stod(value());
            else if (arg == "--transpose") options.transposeSemitones = std::stoi(value());
            else if (arg == "--sample-rate") options.sampleRate = static_cast<uint32_t>(std::stoul(value()));
            else if (arg == "--phonemizer") options.phonemizer = value();
            else if (arg == "--inspect") inspectOnly = true;
            else if (arg == "--help") {
                std::cout << "vocalrack-render --singer FOLDER (--score SCORE.json | --project SCORE.ustx/.ust/.mid) --out FILE.wav"
                             " [--track N] [--solo-note N] [--bpm N] [--transpose N]\n"
                             "vocalrack-render (--score SCORE.json | --vocalrack PROJECT.vocalrack) --export-ustx FILE.ustx"
                             " [--export-singer-id ID] [--phonemizer NAME]\n"
                             "vocalrack-render --inspect --project SCORE.ustx/.ust/.mid\n";
                return 0;
            } else throw std::runtime_error("Unknown option " + arg);
        }
        options.phonemizer = canonicalPhonemizerName(std::move(options.phonemizer));
        if (inspectOnly) {
            if (projectPath.empty() || !scorePath.empty()) throw std::runtime_error("--inspect requires exactly one --project file");
            UstxImporter importer;
            const auto tracks = importer.scanTracks(projectPath);
            std::cout << "Project: " << projectPath.string() << "\nTracks: " << tracks.size() << '\n';
            for (const auto& track : tracks) {
                auto imported = importer.importTrack(projectPath, track.index);
                std::cout << "Track " << track.index << ": " << track.name << " notes=" << imported.score.notes.size()
                          << " endTick=" << imported.score.endTick() << " bpm=" << imported.score.nominalBpm
                          << " warnings=" << imported.report.warnings.size() << " ignored=" << imported.report.ignored.size() << '\n';
            }
            return 0;
        }
        const int scoreInputs = !scorePath.empty() + !projectPath.empty() + !nativeProjectPath.empty();
        if (scoreInputs != 1)
            throw std::runtime_error("exactly one of --score/--project/--vocalrack is required");
        VocalScore score;
        if (!nativeProjectPath.empty()) {
            score = loadProjectFile(nativeProjectPath).score;
        } else if (!projectPath.empty()) {
            auto imported = UstxImporter{}.importTrack(projectPath, trackIndex);
            score = std::move(imported.score);
            for (const auto& item : imported.report.imported) std::cout << "Imported: " << item << '\n';
            for (const auto& item : imported.report.approximated) std::cout << "Approximated: " << item << '\n';
            for (const auto& item : imported.report.warnings) std::cerr << "Import warning: " << item << '\n';
            for (const auto& item : imported.report.ignored) std::cerr << "Import ignored: " << item << '\n';
        } else {
            score = scoreFromJson(readFile(scorePath));
        }
        if (!exportUstxPath.empty()) {
            UstxExportOptions exportOptions;
            exportOptions.singer = exportSingerId;
            exportOptions.phonemizer = options.phonemizer == kJapaneseAutoPhonemizer
                ? "OpenUtau.Plugin.Builtin.JapanesePresampPhonemizer"
                : options.phonemizer == kJapaneseCvvcPhonemizer
                ? "OpenUtau.Plugin.Builtin.JapaneseCVVCPhonemizer"
                : options.phonemizer == kEnglishToJapanesePhonemizer
                ? "OpenUtau.Plugin.Builtin.ENtoJAPhonemizer"
                : options.phonemizer == kEnglishXSampaPhonemizer
                ? "OpenUtau.Plugin.Builtin.EnXSampaPhonemizer"
                : options.phonemizer == kEnglishVccvPhonemizer
                ? "OpenUtau.Plugin.Builtin.EnglishVCCVPhonemizer" : std::string();
            const auto exported = exportUstx(score, exportOptions);
            std::ofstream output(exportUstxPath, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Unable to create USTX " + exportUstxPath.string());
            output << exported.text;
            if (!output) throw std::runtime_error("Unable to finish USTX " + exportUstxPath.string());
            for (const auto& item : exported.preserved) std::cout << "Preserved: " << item << '\n';
            for (const auto& item : exported.approximated) std::cout << "Approximated: " << item << '\n';
            for (const auto& item : exported.nativeOnly) std::cout << "Native project only: " << item << '\n';
            return 0;
        }
        if (singerPath.empty() || outputPath.empty())
            throw std::runtime_error("--singer and --out are required when rendering audio");
        if (soloNote >= 0) {
            if (soloNote >= static_cast<int>(score.notes.size()))
                throw std::runtime_error("--solo-note index is outside the imported score");
            for (size_t i = 0; i < score.notes.size(); ++i) {
                if (i != static_cast<size_t>(soloNote))
                    score.notes[i].dynamicsDb.points = {{0, -120.f}};
            }
        }
        auto singer = Voicebank::load(singerPath, singerPath.filename().string());
        if (!singer.valid()) {
            for (const auto& error : singer.diagnostics.errors) std::cerr << "Singer error: " << error << '\n';
            return 2;
        }
        NativeV1Renderer renderer;
        auto audio = renderer.render(score, singer, options);
        for (const auto& phone : audio.diagnostics.phonemes) {
            std::cout << "Phoneme tick=" << phone.relativeTick << " requested=\"" << phone.requestedAlias
                      << "\" selected=\"" << phone.selectedAlias << "\"";
            if (phone.oto) {
                std::cout << " wav=\"" << phone.oto->wavPath.string() << "\""
                          << " offsetMs=" << phone.oto->offsetMs
                          << " consonantMs=" << phone.oto->consonantMs
                          << " cutoffMs=" << phone.oto->cutoffMs
                          << " preutterMs=" << phone.oto->preutterMs
                          << " overlapMs=" << phone.oto->overlapMs
                          << " sourcePitchHz=" << phone.sourcePitchHz
                          << " renderedFrames=" << phone.renderedFrames
                          << " renderedRms=" << phone.renderedRms;
            }
            std::cout << '\n';
            if (!phone.attempts.empty()) {
                std::cout << "  attempted:";
                for (const auto& attempt : phone.attempts)
                    std::cout << " \"" << attempt.alias << "\"=" << (attempt.found ? "found" : "missing");
                std::cout << '\n';
            }
        }
        for (const auto& warning : audio.diagnostics.warnings) std::cerr << "Warning: " << warning << '\n';
        for (const auto& error : audio.diagnostics.errors) std::cerr << "Render error: " << error << '\n';
        if (audio.samples.empty() || !finiteAudio(audio) || peakAudio(audio) < 1e-5f) throw std::runtime_error("Renderer produced invalid or silent audio");
        writeWavMono16(outputPath, audio);
        std::cout << "Rendered " << audio.samples.size() << " frames at " << audio.sampleRate << " Hz, peak=" << peakAudio(audio)
                  << ", rms=" << rmsAudio(audio) << ", renderSeconds=" << audio.diagnostics.renderSeconds << '\n';
        return audio.diagnostics.errors.empty() ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "vocalrack-render: " << e.what() << '\n'; return 1;
    }
}
