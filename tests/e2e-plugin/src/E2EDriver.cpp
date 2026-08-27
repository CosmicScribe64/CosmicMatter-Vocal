#include <rack.hpp>
#include <patch.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

using namespace rack;
namespace fs = std::filesystem;

Plugin* pluginInstance = nullptr;

namespace {

constexpr size_t kShortCaptureFrames = 96'000 * 13;
constexpr size_t kLongCaptureFrames = 96'000 * 20;

struct Capture {
    std::string filename;
    std::vector<float> samples;
    size_t frames = 0;
    std::array<int64_t, 64> endFrames{};
    size_t endCount = 0;
    double secondaryEnergy = 0.0;

    explicit Capture(std::string name = {}, size_t capacity = kShortCaptureFrames)
        : filename(std::move(name)), samples(capacity) {}
    void push(float sample, bool endRise, float secondary = 0.f) noexcept {
        if (frames < samples.size()) samples[frames] = std::clamp(sample / 5.f, -1.f, 1.f);
        secondaryEnergy += std::abs(secondary);
        if (endRise && endCount < endFrames.size()) endFrames[endCount++] = static_cast<int64_t>(frames);
        ++frames;
    }
};

static void put16(std::ofstream& out, uint16_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(bytes, 2);
}

static void put32(std::ofstream& out, uint32_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8), static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    out.write(bytes, 4);
}

static void writeWav(const fs::path& path, const Capture& capture, uint32_t sampleRate) {
    const size_t frames = std::min(capture.frames, capture.samples.size());
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); put32(out, static_cast<uint32_t>(36 + frames * 2)); out.write("WAVEfmt ", 8);
    put32(out, 16); put16(out, 1); put16(out, 1); put32(out, sampleRate); put32(out, sampleRate * 2); put16(out, 2); put16(out, 16);
    out.write("data", 4); put32(out, static_cast<uint32_t>(frames * 2));
    for (size_t i = 0; i < frames; ++i) put16(out, static_cast<uint16_t>(static_cast<int16_t>(std::lround(capture.samples[i] * 32767.f))));
}

struct Metrics { double peak = 0.0, rms = 0.0, longestQuietSeconds = 0.0; };

static Metrics metrics(const Capture& capture, double sampleRate) {
    Metrics result; double sum = 0.0; size_t quiet = 0, longest = 0;
    const size_t frames = std::min(capture.frames, capture.samples.size());
    for (size_t i = 0; i < frames; ++i) {
        const double value = capture.samples[i]; result.peak = std::max(result.peak, std::abs(value)); sum += value * value;
        if (std::abs(value) < 1e-4) longest = std::max(longest, ++quiet); else quiet = 0;
    }
    result.rms = frames ? std::sqrt(sum / frames) : 0.0; result.longestQuietSeconds = longest / sampleRate; return result;
}

}  // namespace

struct E2EDriver : engine::Module {
    enum OutputId { CLOCK_OUTPUT, RUN_A_OUTPUT, RESET_A_OUTPUT, TRIG_A_OUTPUT, SECTION_C_OUTPUT,
        RUN_B_OUTPUT, RESET_B_OUTPUT, RUN_C_OUTPUT, RESET_C_OUTPUT, TRIG_C_OUTPUT,
        ADSR_GATE_OUTPUT, MOD_OUTPUT, OUTPUTS_LEN };
    enum InputId { AUDIO_A_INPUT, END_A_INPUT, AUDIO_B_INPUT, END_B_INPUT, AUDIO_C_INPUT, END_C_INPUT,
        AUDIO_WORD_INPUT, END_WORD_INPUT, AUDIO_LONG_DRY_INPUT, END_LONG_INPUT, AUDIO_LONG_WET_INPUT,
        AUDIO_VOWEL_RAW_INPUT, AUDIO_VOWEL_SHAPED_INPUT, AUDIO_VOWEL_WET_INPUT, INPUTS_LEN };
    enum LightId { RUN_LIGHT, DONE_LIGHT, LIGHTS_LEN };

    enum Phase { PRE_ROLL, DEFAULT_SOUND, GAP_1, CLOCK_120, ADAPT_60, CLOCK_60, ADAPT_120, PAUSE_RESET,
                 GAP_2, ONE_SHOT, GAP_3, LOOP, GAP_4, SECTIONS, GAP_5, MULTIPLE, GAP_6,
                 TRIGGERED_WORD, GAP_7, SUSTAINED_VOWEL, GAP_8, FULL_SONG, FINISHED };

    std::array<Capture, 15> captures{{
        Capture("default-first-sound.wav"), Capture("clock-120.wav"), Capture("clock-60.wav"),
        Capture("pause-resume-reset.wav"), Capture("one-shot.wav"), Capture("loop.wav"),
        Capture("sections.wav"), Capture("multiple-instances.wav"), Capture("triggered-word.wav"),
        Capture("sustained-vowel-baseline.wav"), Capture("sustained-vowel-modulated.wav"),
        Capture("sustained-vowel-shaped.wav"), Capture("sustained-vowel-reverb.wav"),
        Capture("full-song-dry.wav", kLongCaptureFrames), Capture("full-song-reverb.wav", kLongCaptureFrames)
    }};
    Capture reloadCapture{"reload-sound.wav"};
    std::atomic<bool> finished{false};
    bool reloadMode = false;
    fs::path outputDirectory;
    bool flushed = false;
    int phase = PRE_ROLL;
    int64_t phaseFrame = 0;
    double clockPhase = 0.0;
    double sampleRateSeen = 48000.0;
    bool previousEndA = false, previousEndB = false, previousEndC = false;
    bool previousEndWord = false, previousEndLong = false;

    E2EDriver() {
        config(0, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        const char* outputNames[] = {"Clock", "Run A", "Reset A", "Trigger A", "Section C", "Run B", "Reset B",
            "Run C", "Reset C", "Trigger C", "ADSR gate", "Expression modulation"};
        const char* inputNames[] = {"Audio A", "End A", "Audio B", "End B", "Audio C", "End C", "Word audio",
            "Word end", "Long dry", "Long end", "Long reverb", "Vowel raw", "Vowel shaped", "Vowel reverb"};
        for (int i = 0; i < OUTPUTS_LEN; ++i) configOutput(i, outputNames[i]);
        for (int i = 0; i < INPUTS_LEN; ++i) configInput(i, inputNames[i]);
        configLight(RUN_LIGHT, "Test running"); configLight(DONE_LIGHT, "Results flushed");
        reloadMode = std::getenv("VOCALRACK_E2E_RELOAD") != nullptr;
        if (const char* output = std::getenv("VOCALRACK_E2E_DIR")) outputDirectory = output;
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "outputDirectory", json_string(outputDirectory.string().c_str()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (json_t* value = json_object_get(root, "outputDirectory"); json_is_string(value))
            outputDirectory = json_string_value(value);
        // The second application launch reads file state, which lets the
        // required save/reload pass run even when
        // Rack is launched by macOS rather than a shell-provided environment.
        if (!reloadMode && !outputDirectory.empty())
            reloadMode = fs::exists(outputDirectory / "first.done");
    }

    double durationSeconds(int p) const noexcept {
        static constexpr double durations[] = {5.0, 4.2, 0.75, 4.2, 3.0, 8.0, 3.0, 5.0, 0.75, 4.2, 0.75,
            12.0, 0.75, 8.0, 0.75, 4.2, 0.75, 3.0, 0.75, 8.0, 0.75, 19.0};
        if (reloadMode) return p == PRE_ROLL ? 5.0 : 4.2;
        return p >= PRE_ROLL && p <= FULL_SONG ? durations[p] : 0.0;
    }

    void process(const ProcessArgs& args) override {
        if (finished.load(std::memory_order_relaxed)) return;
        sampleRateSeen = args.sampleRate;
        const double phaseSeconds = phaseFrame / static_cast<double>(args.sampleRate);
        const double bpm = (!reloadMode && (phase == ADAPT_60 || phase == CLOCK_60)) ? 60.0 : 120.0;
        clockPhase += bpm * 24.0 / (60.0 * args.sampleRate);
        if (clockPhase >= 1.0) clockPhase -= std::floor(clockPhase);
        outputs[CLOCK_OUTPUT].setVoltage(clockPhase < 0.08 ? 10.f : 0.f);

        bool runA = false, runB = false, runC = false;
        bool resetA = false, resetB = false, resetC = false, trigA = false, trigC = false;
        bool adsrGate = false;
        float modulation = 0.f;
        float sectionC = 0.f;
        const bool atStart = phaseFrame < 64;
        if (reloadMode) {
            if (phase == DEFAULT_SOUND) { runA = true; resetA = atStart; trigA = atStart; }
        }
        else {
            if (phase == DEFAULT_SOUND || phase == CLOCK_120 || phase == CLOCK_60 || phase == ONE_SHOT
                    || phase == TRIGGERED_WORD || phase == FULL_SONG) {
                runA = true; resetA = atStart; trigA = atStart;
            }
            else if (phase == PAUSE_RESET) {
                runA = phaseSeconds < 1.0 || phaseSeconds >= 2.0;
                resetA = atStart || (phaseSeconds >= 2.75 && phaseSeconds < 2.75 + 64.0 / args.sampleRate);
            }
            else if (phase == LOOP) { runB = true; resetB = atStart; }
            else if (phase == SECTIONS) {
                runC = true; resetC = atStart;
                sectionC = (phaseSeconds >= 2.5 && phaseSeconds < 6.0) ? 0.f : 1.f;
                trigC = atStart || (phaseSeconds >= 6.0 && phaseSeconds < 6.0 + 64.0 / args.sampleRate);
            }
            else if (phase == MULTIPLE) { runA = runB = true; resetA = resetB = atStart; trigA = atStart; }
            else if (phase == SUSTAINED_VOWEL) {
                runB = true; resetB = atStart;
                adsrGate = (phaseSeconds >= 0.2 && phaseSeconds < 2.2)
                    || (phaseSeconds >= 3.5 && phaseSeconds < 5.5);
                if (phaseSeconds >= 3.5 && phaseSeconds < 5.5) modulation = 2.f;
            }
        }
        outputs[RUN_A_OUTPUT].setVoltage(runA ? 10.f : 0.f); outputs[RESET_A_OUTPUT].setVoltage(resetA ? 10.f : 0.f);
        outputs[TRIG_A_OUTPUT].setVoltage(trigA ? 10.f : 0.f); outputs[SECTION_C_OUTPUT].setVoltage(sectionC);
        outputs[RUN_B_OUTPUT].setVoltage(runB ? 10.f : 0.f); outputs[RESET_B_OUTPUT].setVoltage(resetB ? 10.f : 0.f);
        outputs[RUN_C_OUTPUT].setVoltage(runC ? 10.f : 0.f); outputs[RESET_C_OUTPUT].setVoltage(resetC ? 10.f : 0.f);
        outputs[TRIG_C_OUTPUT].setVoltage(trigC ? 10.f : 0.f);
        outputs[ADSR_GATE_OUTPUT].setVoltage(adsrGate ? 10.f : 0.f);
        outputs[MOD_OUTPUT].setVoltage(modulation);

        const bool endA = inputs[END_A_INPUT].getVoltage() >= 1.f, endB = inputs[END_B_INPUT].getVoltage() >= 1.f,
            endC = inputs[END_C_INPUT].getVoltage() >= 1.f, endWord = inputs[END_WORD_INPUT].getVoltage() >= 1.f,
            endLong = inputs[END_LONG_INPUT].getVoltage() >= 1.f;
        const bool riseA = endA && !previousEndA, riseB = endB && !previousEndB, riseC = endC && !previousEndC,
            riseWord = endWord && !previousEndWord, riseLong = endLong && !previousEndLong;
        previousEndA = endA; previousEndB = endB; previousEndC = endC;
        previousEndWord = endWord; previousEndLong = endLong;
        const float audioA = inputs[AUDIO_A_INPUT].getVoltage(), audioB = inputs[AUDIO_B_INPUT].getVoltage(), audioC = inputs[AUDIO_C_INPUT].getVoltage();
        if (reloadMode && phase == DEFAULT_SOUND) reloadCapture.push(audioA, riseA);
        else if (!reloadMode) {
            if (phase == DEFAULT_SOUND) captures[0].push(audioA, riseA);
            else if (phase == CLOCK_120) captures[1].push(audioA, riseA);
            else if (phase == CLOCK_60) captures[2].push(audioA, riseA);
            else if (phase == PAUSE_RESET) captures[3].push(audioA, riseA);
            else if (phase == ONE_SHOT) captures[4].push(audioA, riseA);
            else if (phase == LOOP) captures[5].push(audioB, riseB);
            else if (phase == SECTIONS) captures[6].push(audioC, riseC);
            else if (phase == MULTIPLE) captures[7].push((audioA + audioB) * 0.5f, riseA || riseB, audioB);
            else if (phase == TRIGGERED_WORD) captures[8].push(inputs[AUDIO_WORD_INPUT].getVoltage(), riseWord);
            else if (phase == SUSTAINED_VOWEL) {
                if (phaseSeconds < 2.0) captures[9].push(inputs[AUDIO_VOWEL_RAW_INPUT].getVoltage(), false);
                if (phaseSeconds >= 3.5 && phaseSeconds < 5.5)
                    captures[10].push(inputs[AUDIO_VOWEL_RAW_INPUT].getVoltage(), false);
                captures[11].push(inputs[AUDIO_VOWEL_SHAPED_INPUT].getVoltage(), false);
                captures[12].push(inputs[AUDIO_VOWEL_WET_INPUT].getVoltage(), false);
            }
            else if (phase == FULL_SONG) {
                captures[13].push(inputs[AUDIO_LONG_DRY_INPUT].getVoltage(), riseLong);
                captures[14].push(inputs[AUDIO_LONG_WET_INPUT].getVoltage(), riseLong);
            }
        }
        ++phaseFrame;
        const int finalPhase = reloadMode ? DEFAULT_SOUND : FULL_SONG;
        if (phaseFrame >= static_cast<int64_t>(durationSeconds(phase) * args.sampleRate)) {
            phaseFrame = 0; ++phase;
            if (phase > finalPhase) { phase = FINISHED; finished.store(true, std::memory_order_release); }
        }
        lights[RUN_LIGHT].setBrightness(1.f); lights[DONE_LIGHT].setBrightness(0.f);
    }

    uint64_t moduleUnderruns(int64_t id) const {
        auto* module = APP && APP->engine ? APP->engine->getModule(id) : nullptr;
        if (!module) return UINT64_MAX;
        json_t* data = module->dataToJson();
        json_t* value = data ? json_object_get(data, "debugUnderruns") : nullptr;
        const uint64_t count = json_is_integer(value) ? static_cast<uint64_t>(json_integer_value(value)) : UINT64_MAX;
        if (data) json_decref(data); return count;
    }

    static uint64_t textHash(const char* text) noexcept {
        uint64_t hash = 1469598103934665603ull;
        if (!text) return hash;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
            hash ^= *p; hash *= 1099511628211ull;
        }
        return hash;
    }

    void writeModuleStates(std::ofstream& json) const {
        json << "[";
        for (int64_t id = 1; id <= 6; ++id) {
            auto* module = APP && APP->engine ? APP->engine->getModule(id) : nullptr;
            json_t* data = module ? module->dataToJson() : nullptr;
            auto stringValue = [data](const char* key) {
                json_t* value = data ? json_object_get(data, key) : nullptr;
                return json_is_string(value) ? json_string_value(value) : "";
            };
            auto integerValue = [data](const char* key) {
                json_t* value = data ? json_object_get(data, key) : nullptr;
                return json_is_integer(value) ? static_cast<int64_t>(json_integer_value(value)) : int64_t{-1};
            };
            auto param = [module](size_t index) {
                return module && index < module->params.size() ? module->params[index].getValue() : -999.f;
            };
            if (id > 1) json << ", ";
            json << "{\"id\":" << id << ",\"scoreHash\":" << textHash(stringValue("score"))
                 << ",\"singerId\":\"" << stringValue("singerId") << "\",\"ppqn\":" << integerValue("ppqn")
                 << ",\"quantization\":" << integerValue("sectionQuantization")
                 << ",\"loop\":" << param(2) << ",\"range\":" << param(3)
                 << ",\"transpose\":" << param(5) << ",\"section\":" << param(6) << "}";
            if (data) json_decref(data);
        }
        json << "]";
    }

    void flushResults() {
        if (flushed || !finished.load(std::memory_order_acquire)) return;
        flushed = true;
        const char* output = std::getenv("VOCALRACK_E2E_DIR");
        const fs::path outDir = !outputDirectory.empty() ? outputDirectory :
            (output ? fs::path(output) : fs::path("test-artifacts/e2e"));
        fs::create_directories(outDir);
        const uint32_t rate = static_cast<uint32_t>(std::lround(sampleRateSeen));
        if (reloadMode) {
            writeWav(outDir / reloadCapture.filename, reloadCapture, rate);
            const auto m = metrics(reloadCapture, sampleRateSeen);
            std::ofstream json(outDir / "reload-results.json");
            json << std::setprecision(10) << "{\n  \"sampleRate\": " << rate << ",\n  \"peak\": " << m.peak << ",\n  \"rms\": " << m.rms
                 << ",\n  \"endPulses\": " << reloadCapture.endCount << ",\n  \"underruns\": " << moduleUnderruns(1)
                 << ",\n  \"moduleStates\": ";
            writeModuleStates(json); json << "\n}\n";
            std::ofstream(outDir / "reload.done") << "ok\n";
        }
        else {
            APP->patch->save((outDir / "saved-and-reloaded.vcv").string());
            for (const auto& capture : captures) writeWav(outDir / capture.filename, capture, rate);
            std::ofstream json(outDir / "results.raw.json");
            json << std::setprecision(10) << "{\n  \"sampleRate\": " << rate << ",\n  \"scenarios\": {\n";
            const char* names[] = {"defaultFirstSound", "clock120", "clock60", "pauseResumeReset", "oneShot", "loop",
                "sections", "multipleInstances", "triggeredWord", "sustainedVowelBaseline", "sustainedVowelModulated",
                "sustainedVowelShaped", "sustainedVowelReverb", "fullSongDry", "fullSongReverb"};
            for (size_t i = 0; i < captures.size(); ++i) {
                const auto m = metrics(captures[i], sampleRateSeen);
                json << "    \"" << names[i] << "\": {\"frames\": " << captures[i].frames << ", \"seconds\": " << captures[i].frames / sampleRateSeen
                     << ", \"peak\": " << m.peak << ", \"rms\": " << m.rms << ", \"longestQuietSeconds\": " << m.longestQuietSeconds
                     << ", \"endPulses\": [";
                for (size_t e = 0; e < captures[i].endCount; ++e) { if (e) json << ", "; json << captures[i].endFrames[e]; }
                json << "]"; if (i == 7) json << ", \"secondaryEnergy\": " << captures[i].secondaryEnergy; json << "}" << (i + 1 == captures.size() ? "\n" : ",\n");
            }
            json << "  },\n  \"sectionControl\": {\"requestedIndices\": [1, 0, 1], \"triggerSeconds\": [0.0, 6.0]},\n"
                 << "  \"moduleUnderruns\": [" << moduleUnderruns(1) << ", " << moduleUnderruns(2) << ", "
                 << moduleUnderruns(3) << ", " << moduleUnderruns(4) << ", " << moduleUnderruns(5) << ", "
                 << moduleUnderruns(6) << "],\n  \"moduleStates\": ";
            writeModuleStates(json); json << "\n}\n";
            std::ofstream(outDir / "first.done") << "ok\n";
        }
        lights[RUN_LIGHT].setBrightness(0.f); lights[DONE_LIGHT].setBrightness(1.f);
    }
};

struct ProgressDisplay : widget::OpaqueWidget {
    E2EDriver* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5); nvgFillColor(args.vg, nvgRGB(7, 12, 18)); nvgFill(args.vg);
        nvgFontFaceId(args.vg, APP->window->uiFont->handle); nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFontSize(args.vg, 15);
        nvgFillColor(args.vg, module && module->finished.load() ? nvgRGB(100, 240, 150) : nvgRGB(140, 220, 240));
        const std::string text = module ? (module->finished.load() ? "RESULTS WRITTEN" : "SCENARIO " + std::to_string(module->phase)) : "E2E";
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};

struct E2EDriverWidget : app::ModuleWidget {
    E2EDriverWidget(E2EDriver* module) {
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/E2EDriver.svg")));
        auto* display = new ProgressDisplay; display->module = module; display->box = {{15, 66}, {210, 62}}; addChild(display);
        for (int i = 0; i < E2EDriver::OUTPUTS_LEN; ++i)
            addOutput(createOutputCentered<PJ301MPort>({22.f + (i % 6) * 39.f, i < 6 ? 153.f : 213.f}, module, i));
        for (int i = 0; i < E2EDriver::INPUTS_LEN; ++i)
            addInput(createInputCentered<PJ301MPort>({22.f + (i % 7) * 32.5f, i < 7 ? 278.f : 326.f}, module, i));
        addChild(createLightCentered<MediumLight<BlueLight>>({105, 364}, module, E2EDriver::RUN_LIGHT));
        addChild(createLightCentered<MediumLight<GreenLight>>({135, 364}, module, E2EDriver::DONE_LIGHT));
    }
    void step() override {
        if (auto* module = getModule<E2EDriver>()) {
            module->flushResults();
            if (module->flushed && !module->outputDirectory.empty()) {
                const char* marker = module->reloadMode ? "reload-stop.request" : "first-stop.request";
                if (fs::exists(module->outputDirectory / marker) && APP && APP->window)
                    APP->window->close();
            }
        }
        ModuleWidget::step();
    }
};

Model* modelE2EDriver = createModel<E2EDriver, E2EDriverWidget>("E2EDriver");

void init(Plugin* plugin) {
    pluginInstance = plugin;
    plugin->addModel(modelE2EDriver);
}
