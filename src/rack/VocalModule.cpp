#include "VocalModule.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace vocalrack {

static const char* getString(json_t* root, const char* key, const char* fallback = "") {
    json_t* value = json_object_get(root, key); return json_is_string(value) ? json_string_value(value) : fallback;
}

static int getInt(json_t* root, const char* key, int fallback) {
    json_t* value = json_object_get(root, key);
    if (!json_is_integer(value)) return fallback;
    const auto parsed = json_integer_value(value);
    return parsed >= std::numeric_limits<int>::min() && parsed <= std::numeric_limits<int>::max()
        ? static_cast<int>(parsed) : fallback;
}

static double getReal(json_t* root, const char* key, double fallback) {
    json_t* value = json_object_get(root, key);
    const double parsed = json_is_number(value) ? json_number_value(value) : fallback;
    return std::isfinite(parsed) ? parsed : fallback;
}

VocalModule::VocalModule() : score(makeDefaultScore()), renderSlot(RenderService::instance().createSlot()) {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    auto* play = configButton(PLAY_PARAM, "Play/Pause"); play->description = "Starts or pauses the internal transport when no RUN cable is connected.";
    auto* reset = configButton(RESET_PARAM, "Reset"); reset->description = "Returns the playhead to the start of the active song or section range.";
    auto* loop = configSwitch(LOOP_PARAM, 0.f, 1.f, 0.f, "Loop", {"One-shot", "Loop"}); loop->description = "Repeats the active playback range instead of stopping at its end.";
    auto* range = configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "Playback range", {"Song", "Section"}); range->description = "Chooses whether transport covers the whole song or only the selected section.";
    auto* bpm = configParam(BPM_PARAM, 20.f, 300.f, 120.f, "Internal tempo", " BPM"); bpm->description = "Tempo used when the CLOCK input is disconnected.";
    auto* transpose = configParam(TRANSPOSE_PARAM, -36.f, 36.f, 0.f, "Transpose", " semitones"); transpose->snapEnabled = true; transpose->description = "Shifts all rendered notes without changing the authored score.";
    auto* section = configParam(SECTION_PARAM, 0.f, 63.f, 0.f, "Selected section"); section->snapEnabled = true; section->description = "Selects the playback section; the SECTION CV input overrides this control.";
    auto* pitchAmount = configParam(PITCH_ATTENUVERT_PARAM, -1.f, 1.f, 0.f, "Pitch CV attenuverter"); pitchAmount->description = "Sets the polarity and depth of live pitch modulation.";
    auto* dynAmount = configParam(DYN_ATTENUVERT_PARAM, -1.f, 1.f, 0.f, "Dynamics CV attenuverter"); dynAmount->description = "Sets the polarity and depth of live loudness modulation.";
    auto* vibAmount = configParam(VIB_ATTENUVERT_PARAM, -1.f, 1.f, 0.f, "Vibrato CV attenuverter"); vibAmount->description = "Sets the polarity and depth of live vibrato modulation.";
    auto* formAmount = configParam(FORM_ATTENUVERT_PARAM, -1.f, 1.f, 0.f, "Form/timbre CV attenuverter"); formAmount->description = "Sets the polarity and depth of live formant/timbre modulation.";
    auto* clock = configInput(CLOCK_INPUT, "Clock"); clock->description = "Advances transport using the PPQN resolution selected in the module menu.";
    auto* resetIn = configInput(RESET_INPUT, "Reset"); resetIn->description = "A rising trigger returns transport to the active range start.";
    auto* run = configInput(RUN_INPUT, "Run"); run->description = "A high gate runs transport; the module menu chooses resume or restart behavior.";
    auto* trig = configInput(TRIG_INPUT, "Trigger"); trig->description = "A rising trigger starts the active playback range.";
    auto* pitchIn = configInput(PITCH_INPUT, "Pitch offset"); pitchIn->description = "Applies real-time pitch modulation to the rendered voice.";
    auto* dynIn = configInput(DYN_INPUT, "Dynamics"); dynIn->description = "Applies real-time loudness modulation to the rendered voice.";
    auto* vibIn = configInput(VIB_INPUT, "Vibrato"); vibIn->description = "Applies additional real-time vibrato depth.";
    auto* formIn = configInput(FORM_INPUT, "Form/timbre"); formIn->description = "Applies real-time formant/timbre modulation.";
    auto* sectionIn = configInput(SECTION_INPUT, "Section select"); sectionIn->description = "Selects a section index by voltage.";
    auto* voiceOut = configOutput(VOICE_OUTPUT, "Voice"); voiceOut->description = "Rendered mono vocal audio at Rack signal level.";
    auto* endOut = configOutput(END_OUTPUT, "End-of-range trigger"); endOut->description = "Emits a trigger when one-shot playback reaches the end of its active range.";
    auto* ready = configLight(READY_LIGHT, "Ready"); ready->description = "The current score is rendered and ready for playback.";
    auto* rendering = configLight(RENDER_LIGHT, "Rendering"); rendering->description = "Background rendering is in progress or waiting for its first audio buffer.";
    auto* error = configLight(ERROR_LIGHT, "Error"); error->description = "The singer, alias resolution, or renderer needs attention.";
    auto* underrun = configLight(UNDERRUN_LIGHT, "Buffer underrun"); underrun->description = "Playback requested audio before a rendered buffer was available.";
    publishScoreSnapshot();
}

VocalModule::~VocalModule() { RenderService::instance().cancel(renderSlot); }

std::filesystem::path VocalModule::singerRoot() const {
    if (singerId == "builtin:adachi-rei") return rack::asset::plugin(pluginInstance, "res/singers/adachi-rei");
    return externalSingerPath;
}

void VocalModule::publishScoreSnapshot() {
    auto snapshot = std::make_shared<const VocalScore>(score);
    if (scoreSnapshotOwner_) retiredScoreSnapshots_.push_back(std::move(scoreSnapshotOwner_));
    scoreSnapshotOwner_ = std::move(snapshot);
    rtScore.store(scoreSnapshotOwner_.get(), std::memory_order_release);
    const auto* protectedScore = rtScoreReader.load(std::memory_order_acquire);
    retiredScoreSnapshots_.erase(std::remove_if(retiredScoreSnapshots_.begin(), retiredScoreSnapshots_.end(), [protectedScore](const auto& owner) {
        return owner.get() != protectedScore;
    }), retiredScoreSnapshots_.end());
}

void VocalModule::requestRerender() {
    publishScoreSnapshot();
    desiredRenderBpm.store(effectiveBpm.load(std::memory_order_relaxed), std::memory_order_relaxed);
    desiredTranspose.store(static_cast<int>(std::lround(params[TRANSPOSE_PARAM].getValue())), std::memory_order_relaxed);
    renderRequest.fetch_add(1, std::memory_order_release);
    status.store(ModuleStatus::Rendering, std::memory_order_release);
}

RenderDiagnostics VocalModule::copyCurrentDiagnostics() const {
    // A score edit invalidates the published audio immediately, but the
    // background renderer intentionally keeps ownership of that buffer until
    // its replacement is ready.  Never combine those stale phoneme positions
    // with the new score in either editor view: long regions can otherwise
    // collapse into crossed wedges for a frame (or for the whole render).
    const uint64_t request = renderRequest.load(std::memory_order_acquire);
    if (renderSlot->publishedRequestSerial.load(std::memory_order_acquire) != request)
        return {};
    auto diagnostics = renderSlot->copyDiagnostics();
    if (renderRequest.load(std::memory_order_acquire) != request ||
        renderSlot->publishedRequestSerial.load(std::memory_order_acquire) != request)
        return {};
    return diagnostics;
}

void VocalModule::serviceNonRealtime() {
    const uint64_t request = renderRequest.load(std::memory_order_acquire);
    if (request == servicedRequest_) return;
    if (externalSingerNeedsRelink_) {
        singer.reset();
        singerAvailable.store(false, std::memory_order_release);
        renderSlot->clearAudio();
        status.store(ModuleStatus::SingerMissing, std::memory_order_release);
        servicedRequest_ = request;
        return;
    }
    const auto root = singerRoot();
    const std::string singerKey = singerId + "|" + root.string();
    // A missing path is knowable without touching the voicebank parser. Report
    // it immediately and discard the previously rendered singer/audio so the
    // panel never flashes "Rendering" or plays stale audio for a broken link.
    std::error_code singerPathError;
    const bool singerPathExists = !root.empty() && std::filesystem::is_directory(root, singerPathError);
    if (singerPathError || !singerPathExists) {
        singer.reset();
        singerAvailable.store(false, std::memory_order_release);
        renderSlot->clearAudio();
        servicedSingerKey_.clear();
        status.store(ModuleStatus::SingerMissing, std::memory_order_release);
        servicedRequest_ = request;
        return;
    }
    if (singerLoadFuture_.valid()) {
        if (singerLoadFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            singerAvailable.store(false, std::memory_order_release);
            status.store(ModuleStatus::Rendering, std::memory_order_release);
            return;
        }
        try {
            auto loaded = singerLoadFuture_.get();
            if (singerLoadKey_ == singerKey) {
                singer = std::move(loaded);
                servicedSingerKey_ = singerKey;
            }
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(renderSlot->workerMutex);
            renderSlot->lastError = error.what();
            renderSlot->renderFailed.store(true, std::memory_order_release);
            status.store(ModuleStatus::RenderError, std::memory_order_release);
            servicedRequest_ = request;
            return;
        }
    }
    if (!singer || singerKey != servicedSingerKey_) {
        singer.reset();
        singerAvailable.store(false, std::memory_order_release);
        renderSlot->clearAudio();
        singerLoadKey_ = singerKey;
        const auto loadRoot = root;
        const auto loadId = singerId;
        singerLoadFuture_ = std::async(std::launch::async, [loadRoot, loadId] {
            return std::make_shared<Voicebank>(Voicebank::load(loadRoot, loadId));
        });
        status.store(ModuleStatus::Rendering, std::memory_order_release);
        return;
    }
    if (!singer || !singer->valid()) {
        singerAvailable.store(false, std::memory_order_release);
        renderSlot->clearAudio();
        status.store(singer && std::filesystem::exists(root) ? ModuleStatus::VoicebankInvalid : ModuleStatus::SingerMissing);
        servicedRequest_ = request; return;
    }
    singerAvailable.store(true, std::memory_order_release);
    RenderOptions options;
    options.sampleRate = desiredSampleRate.load(); options.bpm = desiredRenderBpm.load();
    if (!(options.bpm >= 20.0 && options.bpm <= 400.0)) options.bpm = score.nominalBpm;
    options.transposeSemitones = desiredTranspose.load(); options.startTick = 0; options.endTick = score.endTick();
    options.phonemizer = phonemizerName;
    options.requestSerial = request;
    renderSlot->alive.store(true);
    RenderService::instance().submit(renderSlot, score, *singer, options);
    servicedRequest_ = request; status.store(ModuleStatus::Rendering);
}

void VocalModule::process(const ProcessArgs& args) {
    const VocalScore* published = nullptr;
    do {
        published = rtScore.load(std::memory_order_acquire);
        rtScoreReader.store(published, std::memory_order_release);
    } while (published != rtScore.load(std::memory_order_acquire));
    if (published && published != activeRtScore_) { activeRtScore_ = published; transport_.setScore(published); }
    if (!activeRtScore_) {
        rtScoreReader.store(nullptr, std::memory_order_release);
        outputs[VOICE_OUTPUT].setVoltage(0.f); outputs[END_OUTPUT].setVoltage(0.f); return;
    }

    if (playButton_.process(params[PLAY_PARAM].getValue())) panelPlaying.store(!panelPlaying.load());
    transport_.settings.internalBpm = params[BPM_PARAM].getValue();
    transport_.settings.ppqn = ppqn.load(std::memory_order_relaxed);
    transport_.settings.loop = params[LOOP_PARAM].getValue() >= 0.5f;
    transport_.settings.rangeMode = params[RANGE_PARAM].getValue() >= 0.5f ? RangeMode::Section : RangeMode::Song;
    transport_.settings.runRising = static_cast<RunRisingBehavior>(runRisingBehavior.load());
    transport_.settings.sectionQuantization = static_cast<SectionQuantization>(sectionQuantization.load());

    const auto roundedControl = [](float value, int fallback) {
        const double numeric = value;
        return std::isfinite(numeric) && numeric >= std::numeric_limits<int>::min() &&
            numeric <= std::numeric_limits<int>::max()
            ? static_cast<int>(std::lround(value)) : fallback;
    };
    int requestedSection = roundedControl(params[SECTION_PARAM].getValue(), 0);
    if (inputs[SECTION_INPUT].isConnected()) requestedSection = roundedControl(inputs[SECTION_INPUT].getVoltage(), 0);
    requestedSection = std::max(0, requestedSection);
    if (requestedSection != lastSectionRequest_) { transport_.requestSection(static_cast<size_t>(requestedSection)); lastSectionRequest_ = requestedSection; }

    TransportInput input;
    input.clock = inputs[CLOCK_INPUT].getVoltage(); input.clockConnected = inputs[CLOCK_INPUT].isConnected();
    const bool editorReset = editorResetRequested.exchange(false, std::memory_order_acq_rel);
    input.reset = std::max(inputs[RESET_INPUT].getVoltage(),
                           (resetButton_.process(params[RESET_PARAM].getValue()) || editorReset) ? 10.f : 0.f);
    input.trig = inputs[TRIG_INPUT].getVoltage(); input.run = inputs[RUN_INPUT].getVoltage(); input.runConnected = inputs[RUN_INPUT].isConnected();
    input.panelPlaying = panelPlaying.load(std::memory_order_relaxed);
    const auto transportOut = transport_.process(input, args.sampleRate);
    // A completed internal one-shot must also release the panel's latched play
    // state. Otherwise the first subsequent click only clears a stale `true`
    // value and the user has to click a second time to restart.
    if (transportOut.rangeCompleted && !transport_.settings.loop && !input.runConnected)
        panelPlaying.store(false, std::memory_order_relaxed);
    displayPlayheadTick.store(transportOut.playheadTick, std::memory_order_relaxed);
    effectiveBpm.store(transportOut.effectiveBpm, std::memory_order_relaxed);
    if (transportOut.endPulse) endPulse_.trigger(1e-3f);
    outputs[END_OUTPUT].setVoltage(endPulse_.process(args.sampleTime) ? 10.f : 0.f);

    const int transpose = roundedControl(params[TRANSPOSE_PARAM].getValue(), 0);
    if (std::abs(transportOut.effectiveBpm - lastBpmRequest_) > std::max(0.5, lastBpmRequest_ * 0.01) || transpose != lastTransposeRequest_) {
        lastBpmRequest_ = transportOut.effectiveBpm; lastTransposeRequest_ = transpose;
        desiredRenderBpm.store(lastBpmRequest_, std::memory_order_relaxed); desiredTranspose.store(transpose, std::memory_order_relaxed);
        renderRequest.fetch_add(1, std::memory_order_release);
        status.store(ModuleStatus::Rendering, std::memory_order_release);
    }

    const bool singerUsable = singerAvailable.load(std::memory_order_acquire);
    const uint64_t wantedRequest = renderRequest.load(std::memory_order_acquire);
    const bool renderMatchesRequest = renderSlot->publishedRequestSerial.load(std::memory_order_acquire) == wantedRequest;
    const RenderedAudio* audio = singerUsable && renderMatchesRequest ? renderSlot->acquireAudioRt() : nullptr;
    float sample = 0.f;
    bool sampleAvailable = false;
    if (audio && !audio->samples.empty()) {
        const double seconds = (transportOut.playheadTick - audio->startTick) * 60.0 / (audio->bpm * kTicksPerQuarter);
        const double frame = seconds * audio->sampleRate;
        if (frame >= 0.0 && frame < audio->samples.size()) {
            const size_t i = static_cast<size_t>(frame);
            if (i + 1 < audio->samples.size()) {
                const float a = static_cast<float>(frame - i);
                sample = audio->samples[i] + (audio->samples[i + 1] - audio->samples[i]) * a;
            } else {
                // The final frame is valid audio. Requiring an interpolation
                // neighbour incorrectly counted one underrun per loop after
                // enough floating-point transport cycles reached this edge.
                sample = audio->samples[i];
            }
            sampleAvailable = std::isfinite(sample);
            if (!sampleAvailable) sample = 0.f;
            if (sampleAvailable) {
                underrunActive_ = false;
                hasPlayedRenderedAudio_ = true;
            }
        }
    }
    const bool missingRequestedAudio = !sampleAvailable;
    const bool unexpectedUnderrun = transportOut.running && missingRequestedAudio && renderMatchesRequest &&
        singerUsable && hasPlayedRenderedAudio_;
    if (transportOut.running && missingRequestedAudio) {
        // The initial asynchronous render is an expected loading state, not a
        // real-time failure. Count an underrun only if playback had already
        // consumed usable rendered audio and that buffer later disappeared.
        if (unexpectedUnderrun && !underrunActive_) {
            renderSlot->underruns.fetch_add(1);
            underrunActive_ = true;
        }
        if (unexpectedUnderrun) status.store(ModuleStatus::BufferUnderrun, std::memory_order_relaxed);
        else if (renderMatchesRequest) status.store(ModuleStatus::WaitingForBuffer, std::memory_order_relaxed);
    }
    const float fadeStep = args.sampleTime / 0.008f;
    runGain_ += std::clamp((transportOut.running ? 1.f : 0.f) - runGain_, -fadeStep, fadeStep);
    ModulationControls controls;
    controls.pitchCv = inputs[PITCH_INPUT].getVoltage(); controls.dynamicsCv = inputs[DYN_INPUT].getVoltage();
    controls.vibratoCv = inputs[VIB_INPUT].getVoltage(); controls.formCv = inputs[FORM_INPUT].getVoltage();
    controls.pitchAttenuverter = params[PITCH_ATTENUVERT_PARAM].getValue();
    controls.dynamicsAttenuverter = params[DYN_ATTENUVERT_PARAM].getValue();
    controls.vibratoAttenuverter = params[VIB_ATTENUVERT_PARAM].getValue(); controls.formAttenuverter = params[FORM_ATTENUVERT_PARAM].getValue();
    const uint64_t auditionRequest = auditionRequest_.load(std::memory_order_acquire);
    if (auditionRequest != auditionRequestSeen_) {
        auditionRequestSeen_ = auditionRequest;
        auditionPhase_ = 0.0;
        auditionEnvelope_ = 1.f;
    }
    float audition = 0.f;
    if (auditionEnvelope_ > 0.0001f) {
        const double frequency = 440.0 * std::pow(2.0, (auditionMidiNote_.load(std::memory_order_relaxed) - 69) / 12.0);
        auditionPhase_ += frequency / args.sampleRate;
        auditionPhase_ -= std::floor(auditionPhase_);
        constexpr double twoPi = 6.28318530717958647692;
        audition = static_cast<float>((std::sin(twoPi * auditionPhase_) +
                                       0.18 * std::sin(twoPi * 2.0 * auditionPhase_)) *
                                      auditionEnvelope_ * 0.12);
        if (auditionHeld_.load(std::memory_order_relaxed)) {
            auditionEnvelope_ += (1.f - auditionEnvelope_) *
                std::min(1.f, 1.f / (0.003f * args.sampleRate));
        } else {
            // Mouse-up uses a short click-free release. One-shot audition
            // requests share the same decay for keyboard/property edits.
            auditionEnvelope_ *= std::max(0.f, 1.f - 1.f / (0.020f * args.sampleRate));
        }
    } else {
        auditionEnvelope_ = 0.f;
    }
    const float output = (singerUsable ? modulation_.process(sample, controls, args.sampleRate) * runGain_ : 0.f) + audition;
    outputs[VOICE_OUTPUT].setVoltage(std::isfinite(output) ? std::clamp(output * 5.f, -5.f, 5.f) : 0.f);

    if (!singerUsable) {
        // Preserve the specific load error published by the non-real-time side.
    }
    else if (unexpectedUnderrun)
        status.store(ModuleStatus::BufferUnderrun, std::memory_order_relaxed);
    else if (!renderMatchesRequest) status.store(ModuleStatus::Rendering, std::memory_order_relaxed);
    else if (renderSlot->rendering.load()) status.store(ModuleStatus::Rendering, std::memory_order_relaxed);
    else if (renderSlot->renderFailed.load(std::memory_order_acquire)) status.store(ModuleStatus::RenderError, std::memory_order_relaxed);
    else if (audio) {
        if (!audio->diagnostics.errors.empty()) status.store(ModuleStatus::AliasMissing, std::memory_order_relaxed);
        else status.store(ModuleStatus::Ready, std::memory_order_relaxed);
    }
    const auto s = status.load(std::memory_order_relaxed);
    lights[READY_LIGHT].setBrightness(s == ModuleStatus::Ready ? 1.f : 0.f);
    lights[RENDER_LIGHT].setBrightness((s == ModuleStatus::Rendering || s == ModuleStatus::WaitingForBuffer) ? 1.f : 0.f);
    lights[ERROR_LIGHT].setBrightness((s == ModuleStatus::SingerMissing || s == ModuleStatus::VoicebankInvalid || s == ModuleStatus::AliasMissing || s == ModuleStatus::RenderError) ? 1.f : 0.f);
    lights[UNDERRUN_LIGHT].setBrightness(s == ModuleStatus::BufferUnderrun ? 1.f : 0.f);
    if (audio) renderSlot->releaseAudioRt();
    rtScoreReader.store(nullptr, std::memory_order_release);
}

void VocalModule::onSampleRateChange(const SampleRateChangeEvent& e) {
    const float sampleRate = std::isfinite(e.sampleRate) ? std::clamp(e.sampleRate, 8000.f, 384000.f) : 48000.f;
    desiredSampleRate.store(static_cast<uint32_t>(std::lround(sampleRate))); renderRequest.fetch_add(1);
}

json_t* VocalModule::dataToJson() {
    json_t* root = json_object();
    json_object_set_new(root, "stateSchemaVersion", json_integer(1));
    json_object_set_new(root, "score", json_string(scoreToJson(score).c_str()));
    json_object_set_new(root, "singerId", json_string(singerId.c_str()));
    json_object_set_new(root, "externalSingerPath", json_string(externalSingerPath.c_str()));
    json_object_set_new(root, "phonemizer", json_string(phonemizerName.c_str()));
    json_object_set_new(root, "ppqn", json_integer(ppqn.load()));
    json_object_set_new(root, "runRisingBehavior", json_integer(runRisingBehavior.load()));
    json_object_set_new(root, "sectionQuantization", json_integer(sectionQuantization.load()));
    json_object_set_new(root, "panelPlaying", json_boolean(panelPlaying.load()));
    json_object_set_new(root, "editorScrollX", json_real(editorScrollX)); json_object_set_new(root, "editorScrollY", json_real(editorScrollY));
    json_object_set_new(root, "editorZoomX", json_real(editorZoomX)); json_object_set_new(root, "editorZoomY", json_real(editorZoomY));
    json_object_set_new(root, "editorFollowPlayhead", json_boolean(editorFollowPlayhead));
    json_object_set_new(root, "editorSnapEnabled", json_boolean(editorSnapEnabled));
    json_object_set_new(root, "editorSnapTick", json_integer(editorSnapTick));
    json_object_set_new(root, "debugUnderruns", json_integer(static_cast<json_int_t>(renderSlot->underruns.load())));
    json_object_set_new(root, "debugStatus", json_string(statusText().c_str()));
    return root;
}

void VocalModule::dataFromJson(json_t* root) {
    try { score = scoreFromJson(getString(root, "score", scoreToJson(makeDefaultScore()).c_str())); }
    catch (const std::exception& e) { score = makeDefaultScore(); std::lock_guard<std::mutex> lock(renderSlot->workerMutex); renderSlot->lastError = e.what(); status.store(ModuleStatus::RenderError); }
    singerId = getString(root, "singerId", "builtin:adachi-rei"); externalSingerPath = getString(root, "externalSingerPath");
    externalSingerNeedsRelink_ = singerId != "builtin:adachi-rei";
    phonemizerName = canonicalPhonemizerName(
        getString(root, "phonemizer", kEnglishToJapanesePhonemizer));
    ppqn.store(std::clamp(getInt(root, "ppqn", 24), 1, 48));
    runRisingBehavior.store(std::clamp(getInt(root, "runRisingBehavior", 0), 0, 1)); sectionQuantization.store(std::clamp(getInt(root, "sectionQuantization", 3), 0, 3));
    json_t* playing = json_object_get(root, "panelPlaying"); if (json_is_boolean(playing)) panelPlaying.store(json_is_true(playing));
    editorScrollX = static_cast<float>(std::clamp(getReal(root, "editorScrollX", 0), -1.0e9, 1.0e9));
    editorScrollY = static_cast<float>(std::clamp(getReal(root, "editorScrollY", 60), -1.0e6, 1.0e6));
    editorZoomX = std::clamp(static_cast<float>(getReal(root, "editorZoomX", 0.16)), 0.02f, 2.f);
    editorZoomY = std::clamp(static_cast<float>(getReal(root, "editorZoomY", 12)), 4.f, 40.f);
    editorViewInitialized = json_object_get(root, "editorZoomX") || json_object_get(root, "editorScrollX");
    json_t* follow = json_object_get(root, "editorFollowPlayhead");
    editorFollowPlayhead = !json_is_boolean(follow) || json_is_true(follow);
    json_t* snapEnabled = json_object_get(root, "editorSnapEnabled");
    editorSnapEnabled = !json_is_boolean(snapEnabled) || json_is_true(snapEnabled);
    editorSnapTick = std::clamp<int64_t>(getInt(root, "editorSnapTick", 120), 1, 1920);
    publishScoreSnapshot(); renderRequest.fetch_add(1);
}

void VocalModule::replaceScore(VocalScore replacement, const std::string& label) {
    const auto before = scoreToJson(score); replacement.normalize(); replacement.touch(); score = std::move(replacement); commitScoreEdit(before, label);
}

void VocalModule::commitScoreEdit(const std::string& before, const std::string& label) {
    score.normalize();
    const auto errors = score.validate();
    if (!errors.empty()) {
        try { score = scoreFromJson(before); } catch (...) { score = makeDefaultScore(); }
        {
            std::lock_guard<std::mutex> lock(renderSlot->workerMutex);
            renderSlot->lastError = "Edit rejected: " + errors.front();
        }
        status.store(ModuleStatus::RenderError, std::memory_order_release);
        return;
    }
    undoStack_.push_back({label, before}); if (undoStack_.size() > 100) undoStack_.erase(undoStack_.begin()); redoStack_.clear();
    score.touch(); requestRerender();
}

void VocalModule::undo() {
    if (undoStack_.empty()) {
        return;
    }
    auto item = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back({item.first, scoreToJson(score)});
    score = scoreFromJson(item.second);
    score.touch();
    requestRerender();
}

void VocalModule::redo() {
    if (redoStack_.empty()) {
        return;
    }
    auto item = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back({item.first, scoreToJson(score)});
    score = scoreFromJson(item.second);
    score.touch();
    requestRerender();
}

void VocalModule::selectSingerFolder(const std::string& path) {
    singerId = "external:" + path;
    externalSingerPath = path;
    externalSingerNeedsRelink_ = false;
    // English is the neutral default for external banks. A Japanese bank is
    // still one click away. Returning to bundled Adachi Rei changes native
    // English-bank modes to its compatible English-to-Japanese mode.
    phonemizerName = kEnglishXSampaPhonemizer;
    servicedSingerKey_.clear();
    requestRerender();
}

VocalProjectState VocalModule::captureProject() {
    VocalProjectState project;
    project.score = score;
    project.singerId = singerId;
    project.externalSingerPath = externalSingerPath;
    project.phonemizerName = phonemizerName;
    project.ppqn = ppqn.load(std::memory_order_relaxed);
    project.runRisingBehavior = runRisingBehavior.load(std::memory_order_relaxed);
    project.sectionQuantization = sectionQuantization.load(std::memory_order_relaxed);
    project.panelPlaying = panelPlaying.load(std::memory_order_relaxed);
    project.loop = params[LOOP_PARAM].getValue() >= 0.5f;
    project.sectionRange = params[RANGE_PARAM].getValue() >= 0.5f;
    project.bpm = params[BPM_PARAM].getValue();
    project.transpose = static_cast<int>(std::lround(params[TRANSPOSE_PARAM].getValue()));
    project.section = static_cast<int>(std::lround(params[SECTION_PARAM].getValue()));
    project.pitchCvAmount = params[PITCH_ATTENUVERT_PARAM].getValue();
    project.dynamicsCvAmount = params[DYN_ATTENUVERT_PARAM].getValue();
    project.vibratoCvAmount = params[VIB_ATTENUVERT_PARAM].getValue();
    project.formCvAmount = params[FORM_ATTENUVERT_PARAM].getValue();
    project.editorScrollX = editorScrollX;
    project.editorScrollY = editorScrollY;
    project.editorZoomX = editorZoomX;
    project.editorZoomY = editorZoomY;
    project.editorFollowPlayhead = editorFollowPlayhead;
    project.editorSnapEnabled = editorSnapEnabled;
    project.editorSnapTick = editorSnapTick;
    return project;
}

void VocalModule::loadProject(VocalProjectState project) {
    const auto before = scoreToJson(score);
    score = std::move(project.score);
    score.normalize();
    singerId = std::move(project.singerId);
    externalSingerPath = std::move(project.externalSingerPath);
    externalSingerNeedsRelink_ = singerId != "builtin:adachi-rei";
    phonemizerName = canonicalPhonemizerName(std::move(project.phonemizerName));
    ppqn.store(std::clamp(project.ppqn, 1, 96), std::memory_order_relaxed);
    runRisingBehavior.store(std::clamp(project.runRisingBehavior, 0, 1), std::memory_order_relaxed);
    sectionQuantization.store(std::clamp(project.sectionQuantization, 0, 3), std::memory_order_relaxed);
    panelPlaying.store(project.panelPlaying, std::memory_order_relaxed);
    params[LOOP_PARAM].setValue(project.loop ? 1.f : 0.f);
    params[RANGE_PARAM].setValue(project.sectionRange ? 1.f : 0.f);
    params[BPM_PARAM].setValue(std::clamp(project.bpm, 20.f, 300.f));
    params[TRANSPOSE_PARAM].setValue(std::clamp(static_cast<float>(project.transpose), -36.f, 36.f));
    params[SECTION_PARAM].setValue(std::clamp(static_cast<float>(project.section), 0.f, 63.f));
    params[PITCH_ATTENUVERT_PARAM].setValue(std::clamp(project.pitchCvAmount, -1.f, 1.f));
    params[DYN_ATTENUVERT_PARAM].setValue(std::clamp(project.dynamicsCvAmount, -1.f, 1.f));
    params[VIB_ATTENUVERT_PARAM].setValue(std::clamp(project.vibratoCvAmount, -1.f, 1.f));
    params[FORM_ATTENUVERT_PARAM].setValue(std::clamp(project.formCvAmount, -1.f, 1.f));
    editorScrollX = project.editorScrollX;
    editorScrollY = project.editorScrollY;
    editorZoomX = std::clamp(project.editorZoomX, 0.02f, 2.f);
    editorZoomY = std::clamp(project.editorZoomY, 4.f, 40.f);
    editorViewInitialized = true;
    editorFollowPlayhead = project.editorFollowPlayhead;
    editorSnapEnabled = project.editorSnapEnabled;
    editorSnapTick = std::clamp<int64_t>(project.editorSnapTick, 1, 1920);
    servicedSingerKey_.clear();
    commitScoreEdit(before, "Load VocalRack project");
}

void VocalModule::auditionMidiNote(int midiNote) noexcept {
    auditionHeld_.store(false, std::memory_order_relaxed);
    auditionMidiNote_.store(std::clamp(midiNote, 0, 127), std::memory_order_relaxed);
    auditionRequest_.fetch_add(1, std::memory_order_release);
}

void VocalModule::beginAuditionMidiNote(int midiNote) noexcept {
    auditionMidiNote_.store(std::clamp(midiNote, 0, 127), std::memory_order_relaxed);
    auditionHeld_.store(true, std::memory_order_release);
    auditionRequest_.fetch_add(1, std::memory_order_release);
}

void VocalModule::endAudition() noexcept {
    auditionHeld_.store(false, std::memory_order_release);
}

std::string VocalModule::statusText() const {
    switch (status.load()) {
        case ModuleStatus::Ready: return "Ready"; case ModuleStatus::Rendering: return "Rendering";
        case ModuleStatus::WaitingForBuffer: return "Waiting for buffer"; case ModuleStatus::SingerMissing: return "Singer missing";
        case ModuleStatus::VoicebankInvalid: return "Voicebank invalid"; case ModuleStatus::AliasMissing: return "Alias missing";
        case ModuleStatus::ImportWarning: return "Import warning"; case ModuleStatus::RenderError: return "Render error";
        case ModuleStatus::BufferUnderrun: return "Buffer underrun";
    } return "Unknown";
}

std::string VocalModule::singerDisplayName() const { return singer && !singer->character.name.empty() ? singer->character.name : (singerId == "builtin:adachi-rei" ? "足立レイ" : "Singer missing"); }

std::string VocalModule::lastError() const {
    std::lock_guard<std::mutex> lock(renderSlot->workerMutex); return renderSlot->lastError;
}

}  // namespace vocalrack
