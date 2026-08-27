#include "VocalEditor.hpp"
#include "VocalModule.hpp"
#include "core/PitchModel.hpp"

#include <osdialog.h>
#include <cstdlib>
#include <initializer_list>
#include <utility>

namespace vocalrack {

namespace {

void drawPanelTransportIcon(NVGcontext* vg, float cx, float cy, int icon) {
    const NVGcolor color = nvgRGB(217, 223, 235);
    nvgSave(vg);
    nvgStrokeColor(vg, color);
    nvgFillColor(vg, color);
    nvgStrokeWidth(vg, 1.5f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    if (icon == 0) {
        // Play/pause uses the combined transport symbol because the LED button
        // the LED button toggles between both states and Rack's parameter
        // tooltip supplies the full accessible name.
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 8.f, cy - 5.f);
        nvgLineTo(vg, cx - 1.f, cy);
        nvgLineTo(vg, cx - 8.f, cy + 5.f);
        nvgClosePath(vg);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, cx + 2.f, cy - 5.f, 2.f, 10.f);
        nvgRect(vg, cx + 7.f, cy - 5.f, 2.f, 10.f);
        nvgFill(vg);
    } else if (icon == 1) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 7.f, cy - 6.f);
        nvgLineTo(vg, cx - 7.f, cy + 6.f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + 6.f, cy - 6.f);
        nvgLineTo(vg, cx - 4.f, cy);
        nvgLineTo(vg, cx + 6.f, cy + 6.f);
        nvgClosePath(vg);
        nvgFill(vg);
    } else {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 7.f, cy - 2.f);
        nvgBezierTo(vg, cx - 5.f, cy - 6.f, cx + 4.f, cy - 6.f, cx + 6.f, cy - 2.f);
        nvgMoveTo(vg, cx + 7.f, cy + 2.f);
        nvgBezierTo(vg, cx + 5.f, cy + 6.f, cx - 4.f, cy + 6.f, cx - 6.f, cy + 2.f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + 3.f, cy - 5.f); nvgLineTo(vg, cx + 7.f, cy - 2.f); nvgLineTo(vg, cx + 3.f, cy + 1.f);
        nvgMoveTo(vg, cx - 3.f, cy + 5.f); nvgLineTo(vg, cx - 7.f, cy + 2.f); nvgLineTo(vg, cx - 3.f, cy - 1.f);
        nvgStroke(vg);
    }
    nvgRestore(vg);
}

}  // namespace

struct VocalPanelLabels : rack::widget::Widget {
    VocalModule* module = nullptr;
    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, APP->window->uiFont->handle);
        auto text = [vg](float x, float y, float size, NVGcolor color, int align, const char* value) {
            nvgFontSize(vg, size); nvgFillColor(vg, color); nvgTextAlign(vg, align | NVG_ALIGN_MIDDLE); nvgText(vg, x, y, value, nullptr);
        };
        const NVGcolor primary = nvgRGB(217, 223, 235);
        const NVGcolor secondary = nvgRGB(130, 143, 166);
        text(28, 21, 19, nvgRGB(245, 104, 152), NVG_ALIGN_LEFT, "VOCAL");
        text(108, 20, 10, nvgRGB(174, 185, 204), NVG_ALIGN_LEFT, "NATIVE V1  /  UTAU VOICEBANK VOCAL SYNTHESIZER");
        if (module) {
            const auto& score = module->score;
            const double beatTicks = kTicksPerQuarter * 4.0 / std::max(1, score.beatUnit);
            const double barTicks = beatTicks * std::max(1, score.beatsPerBar);
            const int bar = static_cast<int>(module->displayPlayheadTick.load() / barTicks) + 1;
            std::string range = "SONG";
            if (module->params[VocalModule::RANGE_PARAM].getValue() > 0.5f && !score.sections.empty()) {
                const size_t section = std::min<size_t>(
                    std::lround(module->params[VocalModule::SECTION_PARAM].getValue()), score.sections.size() - 1);
                range = score.sections[section].name;
            }
            const std::string transport = module->inputs[VocalModule::RUN_INPUT].isConnected()
                ? "EXTERNAL RUN" : (module->panelPlaying.load() ? "PLAYING" : "PAUSED");
            nvgSave(vg);
            nvgScissor(vg, 355.f, 4.f, 375.f, 32.f);
            text(365, 20, 12, nvgRGB(245, 104, 152), NVG_ALIGN_LEFT,
                 (range + "  /  BAR " + std::to_string(bar) + "  /  " + transport).c_str());
            nvgRestore(vg);
        }
        for (const auto& item : std::initializer_list<std::pair<float, const char*>>{{770, "READY"}, {815, "RENDER"}, {860, "ERROR"}, {905, "UNDER"}})
            text(item.first, 31, 8, secondary, NVG_ALIGN_CENTER, item.second);

        text(24, 50, 9, secondary, NVG_ALIGN_LEFT, "TRANSPORT");
        text(810, 50, 9, secondary, NVG_ALIGN_LEFT, "PERFORMANCE");
        text(810, 130, 9, secondary, NVG_ALIGN_LEFT, "EXPRESSION + CV AMOUNT");
        text(24, 238, 9, secondary, NVG_ALIGN_LEFT, "TRANSPORT IN");
        text(810, 304, 9, secondary, NVG_ALIGN_LEFT, "ROUTING");
        drawPanelTransportIcon(vg, 50.f, 105.f, 0);
        drawPanelTransportIcon(vg, 122.f, 105.f, 1);
        drawPanelTransportIcon(vg, 50.f, 156.f, 2);
        for (const auto& item : std::initializer_list<std::pair<float, const char*>>{
                 {122, "SONG / SECTION"}, {86, "BPM"},
                 {840, "TRANSPOSE"}, {909, "SECTION"},
                 {839, "PITCH"}, {911, "DYN"}, {839, "VIB"}, {911, "FORM"}}) {
            float y = 0.f;
            if (item.second == std::string("SONG / SECTION")) y = 156.f;
            else if (item.second == std::string("BPM")) y = 221.f;
            else if (item.second == std::string("TRANSPOSE") || item.second == std::string("SECTION")) y = 113.f;
            else if (item.second == std::string("PITCH") || item.second == std::string("DYN")) y = 185.f;
            else y = 244.f;
            text(item.first, y, 9, primary, NVG_ALIGN_CENTER, item.second);
        }

        for (const auto& item : std::initializer_list<std::pair<float, const char*>>{
                 {50, "CLOCK"}, {122, "RESET"}, {50, "RUN"}, {122, "TRIG"},
                 {822, "SECTION"}, {875, "VOICE"}, {926, "END"}}) {
            const float y = (item.first == 50 || item.first == 122) && (item.second == std::string("CLOCK") || item.second == std::string("RESET")) ? 291.f
                              : (item.first == 50 || item.first == 122) ? 354.f : 359.f;
            text(item.first, y, 8.5f, primary, NVG_ALIGN_CENTER, item.second);
        }
    }
};

struct TooltipButton : rack::widget::OpaqueWidget {
    std::string tooltipText;
    rack::ui::Tooltip* tooltip = nullptr;
    ~TooltipButton() override { if (tooltip) tooltip->requestDelete(); }
    void onEnter(const EnterEvent&) override {
        if (!tooltip && !tooltipText.empty()) { tooltip = new rack::ui::Tooltip; tooltip->text = tooltipText; APP->scene->addChild(tooltip); }
    }
    void onLeave(const LeaveEvent&) override { if (tooltip) { tooltip->requestDelete(); tooltip = nullptr; } }
    virtual void activate() {}
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            activate();
            e.consume(this);
            return;
        }
        OpaqueWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) activate();
    }
};

struct VocalDisplay : TooltipButton {
    VocalModule* module = nullptr;
    void activate() override { if (module) openVocalEditor(module); }
    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg; nvgBeginPath(vg); nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 5); nvgFillColor(vg, nvgRGB(8, 15, 22)); nvgFill(vg);
        if (!module) return;
        const auto& score = module->score; const double tick = module->displayPlayheadTick.load();
        const float left = 9.f, width = box.size.x - 18.f, keyWidth = 44.f;
        const float timelineLeft = left + keyWidth + 5.f;
        const float pitchTop = 9.f;

        const float timelineWidth = width - keyWidth - 5.f;
        const float pitchBottom = box.size.y - 113.f;
        const float phonemeTop = pitchBottom + 7.f, phonemeBottom = phonemeTop + 28.f;
        const float dynTop = phonemeBottom + 5.f, dynBottom = box.size.y - 29.f;
        const double viewStart = std::max(0.0, tick - 960.0), viewTicks = 3840.0;
        int minMidi = 60, maxMidi = 72; bool foundVisible = false;
        for (const auto& note : score.notes) {
            if (note.endTick() < viewStart || note.startTick > viewStart + viewTicks) continue;
            if (!foundVisible) { minMidi = maxMidi = note.midiNote; foundVisible = true; }
            minMidi = std::min(minMidi, note.midiNote); maxMidi = std::max(maxMidi, note.midiNote);
        }
        const int span = std::max(10, maxMidi - minMidi + 4), center = (minMidi + maxMidi) / 2;
        const int rangeMin = center - span / 2, rangeMax = rangeMin + span;
        const float semitoneHeight = (pitchBottom - pitchTop) / static_cast<float>(span);
        for (int midi = rangeMin; midi <= rangeMax; ++midi) {
            const int pitchClass = ((midi % 12) + 12) % 12;
            const bool black = pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
            const float y = pitchTop + (rangeMax - midi) * semitoneHeight;
            nvgBeginPath(vg); nvgRect(vg, left, y - semitoneHeight * 0.5f, keyWidth, semitoneHeight);
            nvgFillColor(vg, black ? nvgRGB(30, 37, 48) : nvgRGB(207, 216, 229)); nvgFill(vg);
            nvgStrokeColor(vg, nvgRGBA(7, 10, 15, 150)); nvgStrokeWidth(vg, 0.7f); nvgStroke(vg);
            if (pitchClass == 0) { const std::string label = "C" + std::to_string(midi / 12 - 1); nvgFontSize(vg, 10); nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE); nvgFillColor(vg, nvgRGB(44, 51, 64)); nvgText(vg, left + keyWidth - 4.f, y, label.c_str(), nullptr); }
        }
        nvgBeginPath(vg); nvgMoveTo(vg, timelineLeft - 3.f, pitchTop); nvgLineTo(vg, timelineLeft - 3.f, pitchBottom); nvgStrokeColor(vg, nvgRGB(91, 107, 132)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
        for (int i = 0; i <= span; ++i) { const float y = pitchTop + (pitchBottom - pitchTop) * i / static_cast<float>(span); nvgBeginPath(vg); nvgMoveTo(vg, timelineLeft, y); nvgLineTo(vg, timelineLeft + timelineWidth, y); nvgStrokeColor(vg, nvgRGBA(95, 111, 137, i % 12 == 0 ? 75 : 32)); nvgStrokeWidth(vg, i % 12 == 0 ? 1.f : 0.6f); nvgStroke(vg); }
        nvgBeginPath(vg); nvgRoundedRect(vg, left, phonemeTop, width, phonemeBottom - phonemeTop, 2); nvgFillColor(vg, nvgRGB(18, 31, 41)); nvgFill(vg);
        nvgFontSize(vg, 9); nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE); nvgFillColor(vg, nvgRGB(110, 210, 226)); nvgText(vg, left + 4.f, (phonemeTop + phonemeBottom) * 0.5f, "PHONEMES", nullptr);
        nvgBeginPath(vg); nvgRoundedRect(vg, timelineLeft, dynTop, timelineWidth, dynBottom - dynTop, 2); nvgFillColor(vg, nvgRGB(14, 24, 34)); nvgFill(vg);
        auto dynY = [=](float db) { return dynBottom - (std::clamp(db, -24.f, 12.f) + 24.f) / 36.f * (dynBottom - dynTop); };
        for (float db : {12.f, 0.f, -24.f}) {
            const float y = dynY(db); nvgBeginPath(vg); nvgMoveTo(vg, timelineLeft, y); nvgLineTo(vg, timelineLeft + timelineWidth, y);
            nvgStrokeColor(vg, db == 0.f ? nvgRGBA(245, 104, 152, 90) : nvgRGBA(95, 111, 137, 45)); nvgStrokeWidth(vg, db == 0.f ? 1.f : 0.7f); nvgStroke(vg);
        }
        nvgFontSize(vg, 10); nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE); nvgFillColor(vg, nvgRGB(245, 104, 152));
        nvgText(vg, timelineLeft + 4, dynTop + 9, "DYNAMICS  +12 / 0 / -24 dB", nullptr);

        nvgSave(vg); nvgIntersectScissor(vg, timelineLeft, pitchTop, timelineWidth, dynBottom - pitchTop);
        const auto diagnostics = module->renderSlot->copyDiagnostics();
        // Note bodies are a separate pass so a connected phrase can carry one
        // unbranched performed-pitch path across all of them.
        for (const auto& note : score.notes) {
            const float x = timelineLeft + (note.startTick - viewStart) / viewTicks * timelineWidth;
            const float w = note.durationTick / viewTicks * timelineWidth;
            if (x + w < timelineLeft || x > timelineLeft + timelineWidth) continue;
            const float y = pitchTop + (rangeMax - note.midiNote) /
                static_cast<float>(span) * (pitchBottom - pitchTop);
            const float noteHeight = std::clamp((pitchBottom - pitchTop) /
                static_cast<float>(span) * 1.08f, 20.f, 26.f);
            nvgBeginPath(vg); nvgRoundedRect(vg, x, y - noteHeight * 0.5f,
                std::max(3.f, w), noteHeight, 3);
            nvgFillColor(vg, nvgRGB(30, 170, 197)); nvgFill(vg);
            nvgStrokeColor(vg, nvgRGBA(180, 241, 251, 110));
            nvgStrokeWidth(vg, 0.8f); nvgStroke(vg);
        }
        for (size_t phraseStart = 0; phraseStart < score.notes.size();) {
            size_t phraseEnd = phraseStart;
            while (phraseEnd + 1 < score.notes.size() &&
                   score.notes[phraseEnd].endTick() == score.notes[phraseEnd + 1].startTick)
                ++phraseEnd;
            const int64_t contourStart = score.notes[phraseStart].startTick;
            const int64_t contourEnd = std::max<int64_t>(
                contourStart + 1, score.notes[phraseEnd].endTick());
            const float contourPixels = static_cast<float>(contourEnd - contourStart) /
                static_cast<float>(viewTicks) * timelineWidth;
            const int contourSteps = std::clamp(
                static_cast<int>(std::ceil(contourPixels / 3.f)), 2, 480);
            nvgBeginPath(vg);
            for (int step = 0; step <= contourSteps; ++step) {
                const double alpha = step / static_cast<double>(contourSteps);
                const int64_t absoluteTick = contourStart + static_cast<int64_t>(std::llround(
                    (contourEnd - contourStart) * alpha));
                const float px = timelineLeft +
                    (absoluteTick - viewStart) / viewTicks * timelineWidth;
                const float tone = performedAbsoluteMidi(score, absoluteTick, score.nominalBpm);
                const float py = pitchTop + (rangeMax - tone) / static_cast<float>(span) *
                    (pitchBottom - pitchTop);
                if (step == 0) nvgMoveTo(vg, px, py); else nvgLineTo(vg, px, py);
            }
            nvgStrokeColor(vg, nvgRGB(255, 224, 93));
            nvgStrokeWidth(vg, 1.3f); nvgStroke(vg);
            phraseStart = phraseEnd + 1;
        }
        for (size_t noteIndex = 0; noteIndex < score.notes.size(); ++noteIndex) {
            const auto& note = score.notes[noteIndex];
            const float x = timelineLeft + (note.startTick - viewStart) / viewTicks * timelineWidth, w = note.durationTick / viewTicks * timelineWidth;
            if (x + w < timelineLeft || x > timelineLeft + timelineWidth) continue;
            const float y = pitchTop + (rangeMax - note.midiNote) / static_cast<float>(span) * (pitchBottom - pitchTop);
            if (w > 12.f) { nvgFontSize(vg, 15); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor(vg, nvgRGB(255, 255, 255)); nvgText(vg, x + w / 2, y, note.lyric.c_str(), nullptr); }
            std::string resolvedPhoneme = note.aliasOverride.value_or(note.lyric); bool missingPhoneme = false;
            for (const auto& phone : diagnostics.phonemes) if (phone.sourceNoteId == note.id) { resolvedPhoneme = phone.selectedAlias.empty() ? phone.requestedAlias : phone.selectedAlias; missingPhoneme = !phone.oto; break; }
            nvgBeginPath(vg); nvgRoundedRect(vg, x + 1.f, phonemeTop + 3.f, std::max(2.f, w - 2.f), phonemeBottom - phonemeTop - 6.f, 2.f);
            nvgFillColor(vg, missingPhoneme ? nvgRGB(190, 67, 82) : nvgRGB(34, 111, 132)); nvgFill(vg);
            if (w > 22.f) { nvgFontSize(vg, 11); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor(vg, nvgRGB(235, 249, 252)); nvgText(vg, x + w * 0.5f, (phonemeTop + phonemeBottom) * 0.5f, resolvedPhoneme.c_str(), nullptr); }
            nvgBeginPath(vg);
            if (note.dynamicsDb.points.empty()) { nvgMoveTo(vg, x, dynY(0.f)); nvgLineTo(vg, x + w, dynY(0.f)); }
            else { bool first = true; for (const auto& p : note.dynamicsDb.points) { const float px = x + p.tickOffset / viewTicks * timelineWidth; first ? nvgMoveTo(vg, px, dynY(p.value)) : nvgLineTo(vg, px, dynY(p.value)); first = false; } }
            nvgStrokeColor(vg, nvgRGB(255, 105, 165)); nvgStrokeWidth(vg, 2.f); nvgStroke(vg);
            for (const auto& p : note.dynamicsDb.points) { const float px = x + p.tickOffset / viewTicks * timelineWidth; nvgBeginPath(vg); nvgCircle(vg, px, dynY(p.value), 2.5f); nvgFillColor(vg, nvgRGB(255, 190, 215)); nvgFill(vg); }
        }
        const float playX = timelineLeft + (tick - viewStart) / viewTicks * timelineWidth; nvgBeginPath(vg); nvgMoveTo(vg, playX, pitchTop); nvgLineTo(vg, playX, dynBottom); nvgStrokeColor(vg, nvgRGB(255, 255, 255)); nvgStrokeWidth(vg, 1.2f); nvgStroke(vg); nvgRestore(vg);

        const float footerTop = box.size.y - 23.f;
        nvgBeginPath(vg); nvgRect(vg, 0, footerTop, box.size.x, box.size.y - footerTop); nvgFillColor(vg, nvgRGBA(4, 8, 13, 235)); nvgFill(vg);
        const int transpose = static_cast<int>(std::lround(module->params[VocalModule::TRANSPOSE_PARAM].getValue()));
        const std::string transposeText = (transpose >= 0 ? "+" : "") + std::to_string(transpose);
        const std::string footer = module->singerDisplayName() + "  /  " + module->phonemizerName + "  /  " + std::to_string(static_cast<int>(module->effectiveBpm.load())) + " BPM  /  TRANSPOSE " + transposeText;
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE); nvgFontSize(vg, 10); nvgFillColor(vg, nvgRGB(190, 204, 222)); nvgText(vg, 9, box.size.y - 11.f, footer.c_str(), nullptr);
    }
};

struct UiActionButton : TooltipButton {
    std::string label; std::function<void()> action; bool accent = false; bool expandIcon = false;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4);
        nvgFillColor(args.vg, accent ? nvgRGB(72, 30, 53) : nvgRGB(44, 50, 64)); nvgFill(args.vg);
        nvgStrokeColor(args.vg, accent ? nvgRGB(245, 104, 152) : nvgRGB(74, 84, 103)); nvgStrokeWidth(args.vg, 1.f); nvgStroke(args.vg);
        float textX = box.size.x / 2;
        if (expandIcon) {
            const float x = 15.f, y = box.size.y / 2, r = 6.f, s = 3.f;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, x - r, y - s); nvgLineTo(args.vg, x - r, y - r); nvgLineTo(args.vg, x - s, y - r);
            nvgMoveTo(args.vg, x + s, y - r); nvgLineTo(args.vg, x + r, y - r); nvgLineTo(args.vg, x + r, y - s);
            nvgMoveTo(args.vg, x - r, y + s); nvgLineTo(args.vg, x - r, y + r); nvgLineTo(args.vg, x - s, y + r);
            nvgMoveTo(args.vg, x + s, y + r); nvgLineTo(args.vg, x + r, y + r); nvgLineTo(args.vg, x + r, y + s);
            nvgStrokeColor(args.vg, nvgRGB(255, 210, 227)); nvgStrokeWidth(args.vg, 1.5f); nvgStroke(args.vg);
            textX += 7.f;
        }
        nvgFontFaceId(args.vg, APP->window->uiFont->handle); nvgFontSize(args.vg, accent ? 11 : 10); nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgFillColor(args.vg, nvgRGB(235, 239, 245)); nvgText(args.vg, textX, box.size.y / 2, label.c_str(), nullptr);
    }
    void activate() override { if (action) action(); }
};

struct VocalWidget : rack::app::ModuleWidget {
    bool visualEditorPending_ = std::getenv("VOCALRACK_OPEN_EDITOR_ON_LOAD") != nullptr;
    VocalWidget(VocalModule* module) {
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Vocal.svg")));
        auto* labels = new VocalPanelLabels; labels->module = module; labels->box = {{0, 0}, box.size}; addChild(labels);
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0))); addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        auto* display = new VocalDisplay; display->module = module; display->tooltipText = "Click anywhere in the score display to open the full-screen editor"; display->box = {{174, 42}, {612, 320}}; addChild(display);

        auto* editor = new UiActionButton; editor->tooltipText = "Open the full-screen score editor"; editor->accent = true; editor->expandIcon = true; editor->box = {{750, 49}, {28, 28}}; editor->action = [module]{ if (module) openVocalEditor(module); }; addChild(editor);

        addParam(createParamCentered<LEDButton>(Vec(50, 80), module, VocalModule::PLAY_PARAM));
        addParam(createParamCentered<LEDButton>(Vec(122, 80), module, VocalModule::RESET_PARAM));
        addParam(createParamCentered<CKSS>(Vec(50, 132), module, VocalModule::LOOP_PARAM));
        addParam(createParamCentered<CKSS>(Vec(122, 132), module, VocalModule::RANGE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(86, 190), module, VocalModule::BPM_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(840, 82), module, VocalModule::TRANSPOSE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(909, 82), module, VocalModule::SECTION_PARAM));
        const int atten[] = {VocalModule::PITCH_ATTENUVERT_PARAM, VocalModule::DYN_ATTENUVERT_PARAM, VocalModule::VIB_ATTENUVERT_PARAM, VocalModule::FORM_ATTENUVERT_PARAM};
        const float attenX[] = {858.f, 930.f, 858.f, 930.f};
        const float attenY[] = {160.f, 160.f, 219.f, 219.f};
        for (int i = 0; i < 4; ++i) addParam(createParamCentered<Trimpot>(Vec(attenX[i], attenY[i]), module, atten[i]));
        const int inputs[] = {VocalModule::CLOCK_INPUT, VocalModule::RESET_INPUT, VocalModule::RUN_INPUT, VocalModule::TRIG_INPUT, VocalModule::PITCH_INPUT, VocalModule::DYN_INPUT, VocalModule::VIB_INPUT, VocalModule::FORM_INPUT, VocalModule::SECTION_INPUT};
        const float inputX[] = {50.f, 122.f, 50.f, 122.f, 820.f, 892.f, 820.f, 892.f, 822.f};
        const float inputY[] = {267.f, 267.f, 329.f, 329.f, 160.f, 160.f, 219.f, 219.f, 333.f};
        for (int i = 0; i < 9; ++i) addInput(createInputCentered<PJ301MPort>(Vec(inputX[i], inputY[i]), module, inputs[i]));
        addOutput(createOutputCentered<PJ301MPort>(Vec(875, 333), module, VocalModule::VOICE_OUTPUT)); addOutput(createOutputCentered<PJ301MPort>(Vec(926, 333), module, VocalModule::END_OUTPUT));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(770, 18), module, VocalModule::READY_LIGHT)); addChild(createLightCentered<MediumLight<BlueLight>>(Vec(815, 18), module, VocalModule::RENDER_LIGHT));
        addChild(createLightCentered<MediumLight<RedLight>>(Vec(860, 18), module, VocalModule::ERROR_LIGHT)); addChild(createLightCentered<MediumLight<YellowLight>>(Vec(905, 18), module, VocalModule::UNDERRUN_LIGHT));
    }
    void step() override {
        if (auto* module = getModule<VocalModule>()) {
            module->serviceNonRealtime();
            if (visualEditorPending_) { visualEditorPending_ = false; openVocalEditor(module); }
        }
        ModuleWidget::step();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT &&
            rack::math::Rect({174.f, 42.f}, {612.f, 320.f}).contains(e.pos)) {
            if (auto* module = getModule<VocalModule>()) openVocalEditor(module);
            e.consume(this);
            return;
        }
        ModuleWidget::onButton(e);
    }
    void appendContextMenu(Menu* menu) override {
        auto* module = getModule<VocalModule>(); if (!module) return; menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Cosmic Matter: Vocal"));
        menu->addChild(createMenuItem("Open score editor", "", [module]{ openVocalEditor(module); }));
        menu->addChild(createSubmenuItem("Score templates", "", [module](Menu* templates){
            templates->addChild(createSubmenuItem("English", "", [module](Menu* language){
                const auto setEnglishMode = [module] {
                    module->phonemizerName = module->singerId == "builtin:adachi-rei"
                        ? kEnglishToJapanesePhonemizer : kEnglishXSampaPhonemizer;
                };
                language->addChild(createMenuItem("First sound / phrase: we sing a star", "", [module, setEnglishMode]{
                    setEnglishMode();
                    module->replaceScore(makeDefaultScore(), "New English first-sound phrase");
                    module->params[VocalModule::LOOP_PARAM].setValue(0.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(0.f);
                    module->panelPlaying.store(true);
                }));
                language->addChild(createMenuItem("Sustained vowel instrument: a", "", [module, setEnglishMode]{
                    setEnglishMode();
                    module->replaceScore(makeEnglishDroneScore(), "New English sustained vowel instrument");
                    module->params[VocalModule::LOOP_PARAM].setValue(1.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(0.f);
                    module->panelPlaying.store(true);
                }));
                language->addChild(createMenuItem("Triggered word: sing", "", [module, setEnglishMode]{
                    setEnglishMode();
                    module->replaceScore(makeEnglishTriggeredWordScore(), "New English triggered word");
                    module->params[VocalModule::LOOP_PARAM].setValue(0.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(0.f);
                    module->panelPlaying.store(false);
                }));
                language->addChild(createMenuItem("Looping phrase + one-beat rest", "", [module, setEnglishMode]{
                    setEnglishMode();
                    module->replaceScore(makeEnglishLoopPhraseScore(), "New English looping phrase with rest");
                    module->params[VocalModule::LOOP_PARAM].setValue(1.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(1.f);
                    module->params[VocalModule::SECTION_PARAM].setValue(0.f);
                    module->panelPlaying.store(true);
                }));
            }));
            templates->addChild(createSubmenuItem("Japanese", "", [module](Menu* language){
                language->addChild(createMenuItem("First sound / phrase: あだちれいう", "", [module]{
                    module->phonemizerName = kJapaneseAutoPhonemizer;
                    module->replaceScore(makeJapaneseFirstSoundScore(), "New Japanese first-sound phrase");
                    module->params[VocalModule::LOOP_PARAM].setValue(0.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(0.f);
                    module->panelPlaying.store(true);
                }));
                language->addChild(createMenuItem("Sustained vowel instrument: う", "", [module]{
                    module->phonemizerName = kJapaneseAutoPhonemizer;
                    module->replaceScore(makeDroneScore(), "New Japanese sustained vowel instrument");
                    module->params[VocalModule::LOOP_PARAM].setValue(1.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(0.f);
                    module->panelPlaying.store(true);
                }));
                language->addChild(createMenuItem("Triggered word: あだち", "", [module]{
                    module->phonemizerName = kJapaneseAutoPhonemizer;
                    module->replaceScore(makeTriggeredWordScore(), "New Japanese triggered word");
                    module->params[VocalModule::LOOP_PARAM].setValue(0.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(0.f);
                    module->panelPlaying.store(false);
                }));
                language->addChild(createMenuItem("Looping phrase + one-beat rest", "", [module]{
                    module->phonemizerName = kJapaneseAutoPhonemizer;
                    module->replaceScore(makeLoopPhraseScore(), "New Japanese looping phrase with rest");
                    module->params[VocalModule::LOOP_PARAM].setValue(1.f);
                    module->params[VocalModule::RANGE_PARAM].setValue(1.f);
                    module->params[VocalModule::SECTION_PARAM].setValue(0.f);
                    module->panelPlaying.store(true);
                }));
            }));
            templates->addChild(createMenuItem("Blank score", "", [module]{
                VocalScore blank;
                blank.title = "Empty vocal score";
                module->replaceScore(std::move(blank), "New blank score");
                module->params[VocalModule::LOOP_PARAM].setValue(0.f);
                module->params[VocalModule::RANGE_PARAM].setValue(0.f);
                module->panelPlaying.store(false);
            }));
        }));
        menu->addChild(createMenuItem("Load VocalRack / UTAU / OpenUtau / MIDI...", "", [module]{
            try { importVocalScoreFile(module); }
            catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
        }));
        menu->addChild(createMenuItem("Save lossless VocalRack project...", "", [module]{
            try { saveVocalProjectFile(module); }
            catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
        }));
        menu->addChild(createMenuItem("Export OpenUtau USTX...", "", [module]{
            try { exportVocalUstxFile(module); }
            catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
        }));
        menu->addChild(createSubmenuItem("Transport & timing", std::to_string(module->ppqn.load()) + " PPQN", [module](Menu* m){
            m->addChild(createSubmenuItem("Clock resolution", std::to_string(module->ppqn.load()) + " PPQN", [module](Menu* values){ for (int value : {1,2,4,8,12,16,24,48}) values->addChild(createCheckMenuItem(std::to_string(value) + " PPQN", "", [module,value]{ return module->ppqn.load() == value; }, [module,value]{ module->ppqn.store(value); })); }));
            m->addChild(createSubmenuItem("When RUN rises", module->runRisingBehavior.load() == 0 ? "Resume" : "Restart range", [module](Menu* values){ values->addChild(createCheckMenuItem("Resume", "", [module]{ return module->runRisingBehavior.load() == 0; }, [module]{ module->runRisingBehavior.store(0); })); values->addChild(createCheckMenuItem("Restart active range", "", [module]{ return module->runRisingBehavior.load() == 1; }, [module]{ module->runRisingBehavior.store(1); })); }));
            const char* names[] = {"Immediate", "Next beat", "Next bar", "End of active section"};
            m->addChild(createSubmenuItem("Section changes", names[std::clamp(module->sectionQuantization.load(), 0, 3)], [module](Menu* values){ const char* options[] = {"Immediate", "Next beat", "Next bar", "End of active section"}; for (int i = 0; i < 4; ++i) values->addChild(createCheckMenuItem(options[i], "", [module,i]{ return module->sectionQuantization.load() == i; }, [module,i]{ module->sectionQuantization.store(i); })); }));
        }));
        menu->addChild(createSubmenuItem("Singer & phonemizer", module->singerDisplayName(), [module](Menu* m){
            m->addChild(createMenuItem("Use bundled Adachi Rei", CHECKMARK(module->singerId == "builtin:adachi-rei"), [module]{
                module->singerId = "builtin:adachi-rei";
                module->externalSingerPath.clear();
                if (module->phonemizerName == kEnglishXSampaPhonemizer ||
                    module->phonemizerName == kEnglishVccvPhonemizer) {
                    module->phonemizerName = kEnglishToJapanesePhonemizer;
                }
                module->requestRerender();
            }));
            m->addChild(createMenuItem("Select or relink voicebank folder...", "", [module]{ char* p = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr); if (p) { module->selectSingerFolder(p); std::free(p); } }));
            m->addChild(new MenuSeparator);
            m->addChild(createSubmenuItem("Language / phonemizer", module->phonemizerName, [module](Menu* values){ for (const char* name : {kEnglishToJapanesePhonemizer, kEnglishXSampaPhonemizer, kEnglishVccvPhonemizer, kJapaneseAutoPhonemizer, kJapaneseCvvcPhonemizer, kDirectAliasPhonemizer}) values->addChild(createCheckMenuItem(name, "", [module,name]{ return module->phonemizerName == name; }, [module,name]{ module->phonemizerName = name; module->requestRerender(); })); }));
        }));
        menu->addChild(createSubmenuItem("Diagnostics", module->statusText(), [module](Menu* m){
            const auto stats = RenderService::instance().stats();
            m->addChild(createMenuLabel("Status: " + module->statusText()));
            m->addChild(createMenuLabel("Underruns: " + std::to_string(module->renderSlot->underruns.load())));
            m->addChild(createMenuLabel("Workers: " + std::to_string(stats.activeJobs) + " active, " + std::to_string(stats.canceledJobs) + " canceled"));
            m->addChild(createMenuLabel("Chunks: " + std::to_string(stats.completedChunks) + ", last " + std::to_string(stats.lastChunkRenderMicros) + " µs"));
            m->addChild(createMenuLabel("Cache: " + std::to_string(stats.cacheHits) + " hit / " + std::to_string(stats.cacheMisses) + " miss"));
            m->addChild(createMenuLabel("Decoded samples: " + std::to_string(stats.decodedSampleBytes / 1024) + " KiB"));
            m->addChild(createMenuLabel("Retained RT buffers: " + std::to_string(module->renderSlot->retainedBufferCount())));
            m->addChild(createMenuLabel(module->lastError().empty() ? "Last render: no error" : "Last render: " + module->lastError()));
        }));
    }
};

}  // namespace vocalrack

Model* modelVocal = createModel<vocalrack::VocalModule, vocalrack::VocalWidget>("Vocal");
