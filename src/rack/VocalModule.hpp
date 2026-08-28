#pragma once

#include "core/Serialization.hpp"
#include "core/ProjectFile.hpp"
#include "dsp/RealtimeVoiceModulation.hpp"
#include "plugin.hpp"
#include "render/RenderService.hpp"
#include "transport/VocalTransport.hpp"
#include "voicebank/Voicebank.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace vocalrack {

enum class ModuleStatus : int {
    Ready, Rendering, WaitingForBuffer, SingerMissing, VoicebankInvalid,
    AliasMissing, ImportWarning, RenderError, BufferUnderrun
};

struct VocalModule : rack::engine::Module {
    enum ParamId {
        PLAY_PARAM, RESET_PARAM, LOOP_PARAM, RANGE_PARAM, BPM_PARAM, TRANSPOSE_PARAM, SECTION_PARAM,
        PITCH_ATTENUVERT_PARAM, DYN_ATTENUVERT_PARAM, VIB_ATTENUVERT_PARAM, FORM_ATTENUVERT_PARAM,
        PARAMS_LEN
    };
    enum InputId { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, TRIG_INPUT, PITCH_INPUT, DYN_INPUT, VIB_INPUT, FORM_INPUT, SECTION_INPUT, INPUTS_LEN };
    enum OutputId { VOICE_OUTPUT, END_OUTPUT, OUTPUTS_LEN };
    enum LightId { READY_LIGHT, RENDER_LIGHT, ERROR_LIGHT, UNDERRUN_LIGHT, LIGHTS_LEN };

    VocalScore score;
    std::string singerId = "builtin:adachi-rei";
    std::string externalSingerPath;
    std::string phonemizerName = "English to Japanese";
    std::atomic<int> ppqn{24};
    std::atomic<int> runRisingBehavior{static_cast<int>(RunRisingBehavior::Resume)};
    std::atomic<int> sectionQuantization{static_cast<int>(SectionQuantization::EndOfSection)};
    float editorScrollX = 0.f, editorScrollY = 60.f, editorZoomX = 0.16f, editorZoomY = 12.f;
    bool editorFollowPlayhead = true;
    bool editorSnapEnabled = true;
    bool editorViewInitialized = false;
    int64_t editorSnapTick = 120;
    std::string lastImportReport;

    std::shared_ptr<RenderSlot> renderSlot;
    std::shared_ptr<Voicebank> singer;
    std::atomic<ModuleStatus> status{ModuleStatus::WaitingForBuffer};
    std::atomic<double> displayPlayheadTick{0.0};
    std::atomic<double> effectiveBpm{120.0};
    std::atomic<uint64_t> renderRequest{1};
    std::atomic<uint32_t> desiredSampleRate{48000};
    std::atomic<double> desiredRenderBpm{120.0};
    std::atomic<int> desiredTranspose{0};
    std::atomic<const VocalScore*> rtScore{nullptr};
    std::atomic<const VocalScore*> rtScoreReader{nullptr};
    std::atomic<bool> panelPlaying{true};
    std::atomic<bool> editorResetRequested{false};
    std::atomic<bool> singerAvailable{false};

    VocalModule();
    ~VocalModule() override;
    void process(const ProcessArgs& args) override;
    void onSampleRateChange(const SampleRateChangeEvent& e) override;
    json_t* dataToJson() override;
    void dataFromJson(json_t* root) override;

    // Called from the Rack UI thread or a harness, never from process().
    void serviceNonRealtime();
    void requestRerender();
    void replaceScore(VocalScore replacement, const std::string& undoLabel = "Edit score");
    void commitScoreEdit(const std::string& beforeJson, const std::string& undoLabel);
    void undo();
    void redo();
    void selectSingerFolder(const std::string& path);
    VocalProjectState captureProject();
    void loadProject(VocalProjectState project);
    void auditionMidiNote(int midiNote) noexcept;
    void beginAuditionMidiNote(int midiNote) noexcept;
    void endAudition() noexcept;
    std::string statusText() const;
    std::string singerDisplayName() const;
    std::string lastError() const;
    RenderDiagnostics copyCurrentDiagnostics() const;
    bool transportRunningForTest() const noexcept { return transport_.isRunning(); }

private:
    void publishScoreSnapshot();
    std::filesystem::path singerRoot() const;
    VocalTransport transport_;
    RealtimeVoiceModulation modulation_;
    rack::dsp::SchmittTrigger playButton_, resetButton_;
    rack::dsp::PulseGenerator endPulse_;
    std::shared_ptr<const VocalScore> scoreSnapshotOwner_;
    std::vector<std::shared_ptr<const VocalScore>> retiredScoreSnapshots_;
    const VocalScore* activeRtScore_ = nullptr;
    uint64_t servicedRequest_ = 0;
    std::string servicedSingerKey_;
    std::future<std::shared_ptr<Voicebank>> singerLoadFuture_;
    std::string singerLoadKey_;
    bool externalSingerNeedsRelink_ = false;
    std::vector<std::pair<std::string, std::string>> undoStack_, redoStack_;
    int lastSectionRequest_ = -1;
    double lastBpmRequest_ = 120.0;
    int lastTransposeRequest_ = 0;
    float runGain_ = 0.f;
    bool underrunActive_ = false;
    bool hasPlayedRenderedAudio_ = false;
    std::atomic<int> auditionMidiNote_{60};
    std::atomic<uint64_t> auditionRequest_{0};
    std::atomic<bool> auditionHeld_{false};
    uint64_t auditionRequestSeen_ = 0;
    double auditionPhase_ = 0.0;
    float auditionEnvelope_ = 0.f;
};

}  // namespace vocalrack
