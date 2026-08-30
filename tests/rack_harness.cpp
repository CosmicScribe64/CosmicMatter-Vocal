#include "rack/VocalModule.hpp"
#include "rack/FileDialogs.hpp"

#include <osdialog.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

Plugin* pluginInstance = nullptr;
Model* modelVocal = nullptr;
Model* modelSingerPlate = nullptr;

using namespace vocalrack;
namespace fs = std::filesystem;

struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
#define CHECK(x) do { if (!(x)) throw Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK(" #x ") failed"); } while (0)

struct Capture {
    std::vector<float> audio;
    std::vector<int64_t> endRises;
    double finalTick = 0.0;
};

static VocalScore oneBeatScore() {
    VocalScore score;
    score.title = "Rack harness one beat";
    score.nominalBpm = 120.0;
    Note note;
    note.id = makeUuid();
    note.startTick = 0;
    note.durationTick = kTicksPerQuarter;
    note.midiNote = 60;
    note.lyric = "あ";
    score.notes = {note};
    score.sections = {{makeUuid(), "A", 0, 240}, {makeUuid(), "B", 240, 480}};
    score.normalize();
    return score;
}

static void connect(rack::engine::Input& input, float voltage = 0.f) {
    // Rack's engine owns cable bookkeeping in production. The ABI harness marks
    // the port connected directly because no Engine/Cable object is involved.
    input.channels = 1;
    input.setVoltage(voltage);
}

static void waitRendered(VocalModule& module, double sampleRate = 48000.0) {
    rack::engine::Module::SampleRateChangeEvent event{static_cast<float>(sampleRate), static_cast<float>(1.0 / sampleRate)};
    module.onSampleRateChange(event);
    module.requestRerender();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    do {
        module.serviceNonRealtime();
        if (module.renderSlot->audio() && !module.renderSlot->rendering.load()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    throw Failure("render timeout: " + module.statusText() + " " + module.lastError());
}

static void configure(VocalModule& module, double sampleRate = 48000.0) {
    module.replaceScore(oneBeatScore(), "Harness score");
    module.phonemizerName = kJapaneseAutoPhonemizer;
    module.params[VocalModule::BPM_PARAM].setValue(120.f);
    module.params[VocalModule::LOOP_PARAM].setValue(0.f);
    module.panelPlaying.store(false);
    waitRendered(module, sampleRate);
    CHECK(module.singer);
    CHECK(module.singer->valid());
}

static Capture run(VocalModule& module, int64_t frames, double sampleRate, double clockBpm = 0.0,
                   int ppqn = 24, float runVoltage = 10.f, int64_t resetAt = -1, int64_t trigAt = -1,
                   float pitch = 0.f, float dyn = 0.f, float vib = 0.f, float form = 0.f) {
    Capture capture;
    capture.audio.reserve(static_cast<size_t>(frames));
    rack::engine::Module::ProcessArgs args{static_cast<float>(sampleRate), static_cast<float>(1.0 / sampleRate), 0};
    const int64_t interval = clockBpm > 0.0
        ? static_cast<int64_t>(std::llround(sampleRate * 60.0 / (clockBpm * ppqn))) : 0;
    bool previousEnd = false;
    for (int64_t frame = 0; frame < frames; ++frame) {
        args.frame = frame;
        if (module.inputs[VocalModule::CLOCK_INPUT].isConnected())
            module.inputs[VocalModule::CLOCK_INPUT].setVoltage(interval > 0 && frame % interval < 2 ? 10.f : 0.f);
        if (module.inputs[VocalModule::RUN_INPUT].isConnected()) module.inputs[VocalModule::RUN_INPUT].setVoltage(runVoltage);
        if (module.inputs[VocalModule::RESET_INPUT].isConnected()) module.inputs[VocalModule::RESET_INPUT].setVoltage(frame == resetAt ? 10.f : 0.f);
        if (module.inputs[VocalModule::TRIG_INPUT].isConnected()) module.inputs[VocalModule::TRIG_INPUT].setVoltage(frame == trigAt ? 10.f : 0.f);
        module.inputs[VocalModule::PITCH_INPUT].setVoltage(pitch);
        module.inputs[VocalModule::DYN_INPUT].setVoltage(dyn);
        module.inputs[VocalModule::VIB_INPUT].setVoltage(vib);
        module.inputs[VocalModule::FORM_INPUT].setVoltage(form);
        module.process(args);
        if ((frame & 2047) == 0) module.serviceNonRealtime();
        capture.audio.push_back(module.outputs[VocalModule::VOICE_OUTPUT].getVoltage());
        const bool end = module.outputs[VocalModule::END_OUTPUT].getVoltage() >= 1.f;
        if (end && !previousEnd) capture.endRises.push_back(frame);
        previousEnd = end;
    }
    capture.finalTick = module.displayPlayheadTick.load();
    return capture;
}

static double energy(const std::vector<float>& samples, size_t begin, size_t end) {
    end = std::min(end, samples.size());
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) sum += std::abs(samples[i]);
    return sum;
}

static void testClockTiming() {
    VocalModule module;
    configure(module);
    connect(module.inputs[VocalModule::CLOCK_INPUT]);
    connect(module.inputs[VocalModule::RUN_INPUT], 0.f);
    connect(module.inputs[VocalModule::RESET_INPUT]);
    run(module, 5000, 48000, 120, 24, 0.f);
    auto at120 = run(module, 30000, 48000, 120, 24, 10.f);
    CHECK(at120.endRises.size() == 1);
    CHECK(std::abs(at120.endRises.front() - 24000) < 400);
    CHECK(energy(at120.audio, 2500, 21000) > 50.0);
    // Check well after the voicebank's intentional onset/preutterance rather
    // than inside its leading consonant silence. This window still lies
    // strictly between two 24-PPQN clock edges.
    CHECK(energy(at120.audio, 2500, 2900) > 0.01);

    module.inputs[VocalModule::RUN_INPUT].setVoltage(0.f);
    run(module, 64, 48000, 60, 24, 0.f, 0);
    run(module, 9000, 48000, 60, 24, 0.f);
    auto at60 = run(module, 54000, 48000, 60, 24, 10.f);
    CHECK(at60.endRises.size() == 1);
    std::cout << "INFO  60 BPM END frame=" << at60.endRises.front() << '\n';
    CHECK(std::abs(at60.endRises.front() - 48000) < 600);
}

static void testTransportAndSections() {
    VocalModule module;
    configure(module);
    connect(module.inputs[VocalModule::RUN_INPUT], 10.f);
    connect(module.inputs[VocalModule::RESET_INPUT]);
    connect(module.inputs[VocalModule::TRIG_INPUT]);
    auto first = run(module, 6000, 48000, 0, 24, 10.f);
    CHECK(energy(first.audio, 2000, 5500) > 5.0);
    auto paused = run(module, 3000, 48000, 0, 24, 0.f);
    CHECK(energy(paused.audio, 1000, 3000) < 0.1);
    const double held = paused.finalTick;
    auto resumed = run(module, 4000, 48000, 0, 24, 10.f);
    CHECK(resumed.finalTick > held + 50.0);
    auto reset = run(module, 64, 48000, 0, 24, 10.f, 0);
    CHECK(reset.finalTick < 2.0);
    run(module, 500, 48000, 0, 24, 0.f);
    auto trig = run(module, 1000, 48000, 0, 24, 10.f, -1, 0);
    CHECK(trig.finalTick > 1.0 && trig.finalTick < 30.0);

    module.params[VocalModule::LOOP_PARAM].setValue(1.f);
    run(module, 64, 48000, 0, 24, 0.f, 0);
    auto looped = run(module, 76000, 48000, 0, 24, 10.f);
    CHECK(looped.endRises.size() >= 3);

    module.params[VocalModule::LOOP_PARAM].setValue(0.f);
    run(module, 64, 48000, 0, 24, 0.f, 0);
    auto once = run(module, 60000, 48000, 0, 24, 10.f);
    CHECK(once.endRises.size() == 1);
    CHECK(!module.transportRunningForTest());
    CHECK(energy(once.audio, 50000, 60000) < 0.1);

    module.params[VocalModule::RANGE_PARAM].setValue(1.f);
    module.params[VocalModule::LOOP_PARAM].setValue(1.f);
    module.sectionQuantization.store(static_cast<int>(SectionQuantization::EndOfSection));
    module.params[VocalModule::SECTION_PARAM].setValue(0.f);
    run(module, 64, 48000, 0, 24, 0.f, 0);
    run(module, 6000, 48000, 0, 24, 10.f);
    module.params[VocalModule::SECTION_PARAM].setValue(1.f);
    auto section = run(module, 10000, 48000, 0, 24, 10.f);
    CHECK(!section.endRises.empty());
    CHECK(module.displayPlayheadTick.load() >= 240.0);

    // Internal one-shot regression: after reaching END, a single panel play
    // action must restart. This used to require two clicks because the panel
    // latch stayed true after transport had stopped itself.
    VocalModule panelOnly;
    configure(panelOnly);
    panelOnly.panelPlaying.store(true);
    auto completed = run(panelOnly, 30000, 48000);
    CHECK(completed.endRises.size() == 1);
    CHECK(!panelOnly.panelPlaying.load());
    CHECK(!panelOnly.transportRunningForTest());
    panelOnly.panelPlaying.store(true);  // one Play click
    auto restarted = run(panelOnly, 2000, 48000);
    CHECK(panelOnly.transportRunningForTest());
    CHECK(restarted.finalTick > 10.0);
}

static void testModulationPersistenceRatesAndMissingSinger() {
    VocalModule module;
    configure(module);
    connect(module.inputs[VocalModule::RUN_INPUT], 10.f);
    connect(module.inputs[VocalModule::PITCH_INPUT]);
    connect(module.inputs[VocalModule::DYN_INPUT]);
    connect(module.inputs[VocalModule::VIB_INPUT]);
    connect(module.inputs[VocalModule::FORM_INPUT]);
    auto modulated = run(module, 16000, 48000, 0, 24, 10.f, -1, -1, 5.f, 5.f, 5.f, 5.f);
    CHECK(energy(modulated.audio, 3000, 15000) > 20.0);
    for (float sample : modulated.audio) { CHECK(std::isfinite(sample)); CHECK(std::abs(sample) <= 5.001f); }

    // A score request must immediately make the prior publication ineligible.
    // Drain the short modulation delay without servicing the UI-side request;
    // old lyrics must not continue as a sustained output buffer.
    const auto priorPublication = module.renderSlot->publishedRequestSerial.load();
    module.score.notes.front().lyric = "い";
    module.requestRerender();
    CHECK(module.renderRequest.load() != priorPublication);
    // Editor diagnostics must obey the same generation gate as audio.  Using
    // the prior phone positions with the edited score made first-open phoneme
    // regions collapse into tiny crossed wedges until the render completed.
    CHECK(module.copyCurrentDiagnostics().phonemes.empty());
    rack::engine::Module::ProcessArgs args{48000.f, 1.f / 48000.f, 0};
    double staleTail = 0.0;
    for (int frame = 0; frame < 4096; ++frame) {
        args.frame = frame;
        module.process(args);
        if (frame >= 3072) staleTail += std::abs(module.outputs[VocalModule::VOICE_OUTPUT].getVoltage());
    }
    CHECK(staleTail < 0.1);
    CHECK(module.status.load() == ModuleStatus::Rendering);
    CHECK(module.lights[VocalModule::RENDER_LIGHT].getBrightness() > 0.5f);
    waitRendered(module);
    CHECK(!module.copyCurrentDiagnostics().phonemes.empty());

    module.phonemizerName = "Direct Alias";
    module.ppqn.store(12);
    module.editorZoomX = 0.25f;
    module.editorFollowPlayhead = false;
    module.editorSnapEnabled = false;
    module.editorSnapTick = 60;
    const auto expected = scoreToJson(module.score);
    json_t* state = module.dataToJson();
    VocalModule restored;
    restored.dataFromJson(state);
    json_decref(state);
    CHECK(scoreToJson(restored.score) == expected);
    CHECK(restored.phonemizerName == "Direct Alias");
    CHECK(restored.ppqn.load() == 12);
    CHECK(std::abs(restored.editorZoomX - 0.25f) < 1e-6f);
    CHECK(!restored.editorFollowPlayhead);
    CHECK(!restored.editorSnapEnabled);
    CHECK(restored.editorSnapTick == 60);
    CHECK(restored.editorViewInitialized);

    json_t* untrustedState = module.dataToJson();
    json_object_set_new(untrustedState, "singerId", json_string("external:/"));
    json_object_set_new(untrustedState, "externalSingerPath", json_string("/"));
    VocalModule untrustedRestore;
    untrustedRestore.dataFromJson(untrustedState);
    json_decref(untrustedState);
    untrustedRestore.serviceNonRealtime();
    CHECK(untrustedRestore.status.load() == ModuleStatus::SingerMissing);
    CHECK(!untrustedRestore.singerAvailable.load());

    for (double rate : {44100.0, 48000.0, 96000.0}) {
        VocalModule atRate;
        configure(atRate, rate);
        connect(atRate.inputs[VocalModule::RUN_INPUT], 10.f);
        auto captured = run(atRate, static_cast<int64_t>(rate * 0.3), rate);
        CHECK(energy(captured.audio, static_cast<size_t>(rate * 0.08), captured.audio.size()) > 1.0);
    }

    module.singerId = "external:missing";
    module.externalSingerPath = "/definitely/not/a/vocalrack/singer";
    module.requestRerender();
    const auto missingDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        module.serviceNonRealtime();
        if (module.status.load() == ModuleStatus::SingerMissing) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < missingDeadline);
    CHECK(module.status.load() == ModuleStatus::SingerMissing);
    auto silent = run(module, 4000, 48000, 0, 24, 10.f);
    CHECK(energy(silent.audio, 1000, 4000) < 0.01);
}

static void testMultipleInstances() {
    VocalModule a, b, c;
    configure(a); configure(b); configure(c);
    connect(a.inputs[VocalModule::RUN_INPUT], 10.f);
    connect(b.inputs[VocalModule::RUN_INPUT], 10.f);
    connect(c.inputs[VocalModule::RUN_INPUT], 10.f);
    auto ca = run(a, 12000, 48000); auto cb = run(b, 12000, 48000); auto cc = run(c, 12000, 48000);
    CHECK(energy(ca.audio, 3000, 11000) > 5.0);
    CHECK(energy(cb.audio, 3000, 11000) > 5.0);
    CHECK(energy(cc.audio, 3000, 11000) > 5.0);
}

static void testEditorPitchAudition() {
    VocalModule module;
    configure(module);
    module.panelPlaying.store(false);
    run(module, 256, 48000);
    module.beginAuditionMidiNote(69);
    auto held = run(module, 24000, 48000);
    CHECK(energy(held.audio, 0, 6000) > 20.0);
    CHECK(energy(held.audio, 18000, 24000) > 20.0);
    module.endAudition();
    auto released = run(module, 12000, 48000);
    CHECK(energy(released.audio, 9000, 12000) < 1.0);
    module.auditionMidiNote(72);
    auto oneShot = run(module, 12000, 48000);
    CHECK(energy(oneShot.audio, 0, 5000) > 20.0);
    CHECK(energy(oneShot.audio, 9000, 12000) < 1.0);
    for (float sample : held.audio) {
        CHECK(std::isfinite(sample));
        CHECK(std::abs(sample) <= 5.001f);
    }
}

static void testNativeFileDialogFilter() {
    osdialog_filters* filters = osdialog_filters_parse(kVocalScoreDialogFilterSpec);
    CHECK(filters != nullptr);
    CHECK(filters->name != nullptr);
    CHECK(std::string(filters->name) == "Vocal project or score");
    std::vector<std::string> patterns;
    for (osdialog_filter_patterns* pattern = filters->patterns; pattern; pattern = pattern->next)
        patterns.emplace_back(pattern->pattern);
    osdialog_filters_free(filters);
    CHECK((patterns == std::vector<std::string>{"vocalrack", "ust", "ustx", "mid", "midi"}));
}

static void testProjectAcrossInstances() {
    VocalModule source, destination;
    configure(source); configure(destination);
    source.score.notes[0].pitchCents.points = {{0, -25.f}, {240, 40.f}, {480, 0.f}};
    source.score.notes[0].dynamicsDb.points = {{0, -4.f}, {480, 2.f}};
    source.score.notes[0].vibrato.depthCents = 33.f;
    source.score.notes[0].phonemeTiming.preutteranceDeltaMs = 7.f;
    source.score.touch();
    source.params[VocalModule::LOOP_PARAM].setValue(1.f);
    source.params[VocalModule::BPM_PARAM].setValue(143.f);
    source.params[VocalModule::TRANSPOSE_PARAM].setValue(-5.f);
    source.params[VocalModule::DYN_ATTENUVERT_PARAM].setValue(0.65f);
    source.editorSnapEnabled = false;
    source.editorSnapTick = 31;
    source.ppqn.store(48);
    const auto serialized = projectToJson(source.captureProject(), true);
    destination.loadProject(projectFromJson(serialized));
    const auto restored = destination.captureProject();
    auto restoredScore = restored.score;
    restoredScore.revision = source.score.revision;  // load invalidates render cache by design.
    CHECK(scoreToJson(restoredScore) == scoreToJson(source.score));
    CHECK(restored.loop); CHECK(restored.ppqn == 48);
    CHECK(std::abs(restored.bpm - 143.f) < 0.001f);
    CHECK(restored.transpose == -5);
    CHECK(std::abs(restored.dynamicsCvAmount - 0.65f) < 0.001f);
    CHECK(!restored.editorSnapEnabled); CHECK(restored.editorSnapTick == 31);
    destination.score.notes[0].lyric = "い";
    CHECK(source.score.notes[0].lyric == "あ");
}

static void processThree(VocalModule& a, VocalModule& b, VocalModule& c, int64_t frames, double sampleRate) {
    rack::engine::Module::ProcessArgs args{static_cast<float>(sampleRate), static_cast<float>(1.0 / sampleRate), 0};
    for (int64_t frame = 0; frame < frames; ++frame) {
        args.frame = frame; a.process(args); b.process(args); c.process(args);
    }
}

static void testStressTargets() {
    VocalModule a, b, c;
    configure(a); configure(b); configure(c);
    for (auto* module : {&a, &b, &c}) {
        module->params[VocalModule::LOOP_PARAM].setValue(1.f);
        connect(module->inputs[VocalModule::RUN_INPUT], 10.f);
    }
    processThree(a, b, c, 5LL * 60 * 48000, 48000.0);
    CHECK(a.renderSlot->underruns.load() == 0);
    CHECK(b.renderSlot->underruns.load() == 0);
    CHECK(c.renderSlot->underruns.load() == 0);

    connect(a.inputs[VocalModule::RESET_INPUT]);
    rack::engine::Module::ProcessArgs args{48000.f, 1.f / 48000.f, 0};
    for (int cycle = 0; cycle < 1000; ++cycle) {
        a.inputs[VocalModule::RESET_INPUT].setVoltage(10.f); a.process(args);
        a.inputs[VocalModule::RESET_INPUT].setVoltage(0.f); a.process(args);
    }
    CHECK(a.transportRunningForTest());

    for (int edit = 0; edit < 50; ++edit) {
        a.score.touch(); a.requestRerender(); waitRendered(a);
        a.process(args);
        CHECK(a.renderSlot->retainedBufferCount() <= 2);
    }
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto temporary = std::make_unique<VocalModule>();
        if (iteration < 10) temporary->serviceNonRealtime();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (RenderService::instance().stats().activeJobs && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(RenderService::instance().stats().activeJobs == 0);
    CHECK(RenderService::instance().stats().completedChunks >= 6);
}

int main(int argc, char** argv) {
    rack::plugin::Plugin plugin;
    plugin.path = fs::current_path().string();
    plugin.slug = "CosmicMatter";
    pluginInstance = &plugin;
    std::vector<std::pair<std::string, void (*)()>> tests = {
        {"clock timing", testClockTiming},
        {"transport and sections", testTransportAndSections},
        {"modulation persistence rates and missing singer", testModulationPersistenceRatesAndMissingSinger},
        {"multiple instances", testMultipleInstances},
        {"editor pitch audition", testEditorPitchAudition},
        {"native file-dialog filter", testNativeFileDialogFilter},
        {"lossless project across instances", testProjectAcrossInstances},
    };
    if (argc > 1 && std::string(argv[1]) == "--stress") tests.push_back({"stability targets", testStressTargets});
    int failures = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS  rack module " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL  rack module " << test.first << ": " << error.what() << '\n'; }
    }
    RenderService::instance().shutdown();
    std::cout << (failures ? "FAILED " : "PASSED ") << tests.size() << " Rack ABI harness groups, failures=" << failures << '\n';
    return failures ? 1 : 0;
}
