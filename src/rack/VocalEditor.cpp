#include "VocalEditor.hpp"

#include "core/PitchModel.hpp"
#include "import/UstxImporter.hpp"
#include "export/UstxExporter.hpp"
#include "rack/FileDialogs.hpp"

#include <osdialog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace vocalrack {

namespace {

VocalEditor* gOpenEditor = nullptr;

bool isJapanesePhonemizer(const std::string& name) {
    return name == kJapaneseAutoPhonemizer || name == kJapaneseCvvcPhonemizer;
}

bool usesJapaneseAliases(const std::string& name) {
    return isJapanesePhonemizer(name) || name == kEnglishToJapanesePhonemizer;
}

std::string trimEditorText(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::vector<std::string> splitPhonemeOverrides(const std::string& value) {
    std::vector<std::string> result;
    size_t begin = 0;
    while (begin <= value.size()) {
        const size_t end = value.find('|', begin);
        result.push_back(trimEditorText(value.substr(begin,
            end == std::string::npos ? std::string::npos : end - begin)));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

std::string joinPhonemes(const std::vector<std::string>& aliases) {
    std::ostringstream out;
    for (size_t index = 0; index < aliases.size(); ++index) {
        if (index) out << " | ";
        out << (aliases[index].empty() ? "AUTO" : aliases[index]);
    }
    return out.str();
}

const NVGcolor kBackdrop = nvgRGBA(5, 8, 13, 235);
const NVGcolor kWindow = nvgRGB(19, 24, 33);
const NVGcolor kSurface = nvgRGB(24, 30, 40);
const NVGcolor kSurfaceRaised = nvgRGB(34, 42, 54);
const NVGcolor kBorder = nvgRGB(66, 78, 98);
const NVGcolor kGrid = nvgRGBA(108, 123, 148, 35);
const NVGcolor kGridStrong = nvgRGBA(128, 147, 178, 85);
const NVGcolor kText = nvgRGB(235, 240, 247);
const NVGcolor kTextMuted = nvgRGB(164, 176, 194);
const NVGcolor kPink = nvgRGB(245, 104, 152);
const NVGcolor kCyan = nvgRGB(80, 198, 218);
const NVGcolor kYellow = nvgRGB(255, 220, 84);
const NVGcolor kGreen = nvgRGB(103, 224, 157);
const NVGcolor kDanger = nvgRGB(217, 77, 93);
// Rack's full-editor canvas is commonly viewed on a 13-inch laptop.  The
// stock UI font sizes read as miniature at that scale, so keep the editor's
// type a deliberate step larger than the compact module panel.
constexpr float kEditorTextScale = 1.30f;

void fillRect(NVGcontext* vg, const rack::math::Rect& rect, NVGcolor color, float radius = 0.f) {
    nvgBeginPath(vg);
    if (radius > 0.f) nvgRoundedRect(vg, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y, radius);
    else nvgRect(vg, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void strokeRect(NVGcontext* vg, const rack::math::Rect& rect, NVGcolor color, float width = 1.f, float radius = 0.f) {
    nvgBeginPath(vg);
    if (radius > 0.f) nvgRoundedRect(vg, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y, radius);
    else nvgRect(vg, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

void text(NVGcontext* vg, float x, float y, float size, NVGcolor color, const std::string& value,
          int align = NVG_ALIGN_LEFT) {
    nvgFontFaceId(vg, APP->window->uiFont->handle);
    nvgFontSize(vg, size * kEditorTextScale);
    nvgFillColor(vg, color);
    nvgTextAlign(vg, align | NVG_ALIGN_MIDDLE);
    nvgText(vg, x, y, value.c_str(), nullptr);
}

void textBox(NVGcontext* vg, float x, float y, float width, float size, NVGcolor color,
             const std::string& value, float lineHeight = 1.15f) {
    nvgFontFaceId(vg, APP->window->uiFont->handle);
    nvgFontSize(vg, size * kEditorTextScale);
    nvgFillColor(vg, color);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, lineHeight);
    nvgTextBox(vg, x, y, width, value.c_str(), nullptr);
}

void drawTransportIcon(NVGcontext* vg, const rack::math::Rect& rect, int action,
                       bool playing, NVGcolor color) {
    const float cx = rect.pos.x + rect.size.x * 0.5f;
    const float cy = rect.pos.y + rect.size.y * 0.5f;
    nvgSave(vg);
    nvgStrokeColor(vg, color);
    nvgFillColor(vg, color);
    nvgStrokeWidth(vg, 1.8f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    if (action == 28) {
        if (playing) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 5.5f, cy - 7.f, 3.5f, 14.f, 1.f);
            nvgRoundedRect(vg, cx + 2.f, cy - 7.f, 3.5f, 14.f, 1.f);
            nvgFill(vg);
        } else {
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - 5.f, cy - 7.5f);
            nvgLineTo(vg, cx + 7.f, cy);
            nvgLineTo(vg, cx - 5.f, cy + 7.5f);
            nvgClosePath(vg);
            nvgFill(vg);
        }
    } else if (action == 29) {
        // Return-to-start: the standard transport bar followed by a back cue.
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 7.f, cy - 7.f);
        nvgLineTo(vg, cx - 7.f, cy + 7.f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + 6.f, cy - 7.f);
        nvgLineTo(vg, cx - 4.f, cy);
        nvgLineTo(vg, cx + 6.f, cy + 7.f);
        nvgClosePath(vg);
        nvgFill(vg);
    } else if (action == 30) {
        // Two compact arrows form a familiar loop/repeat glyph.
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 7.f, cy - 2.f);
        nvgBezierTo(vg, cx - 6.f, cy - 7.f, cx + 4.f, cy - 7.f, cx + 6.f, cy - 3.f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + 3.f, cy - 6.f);
        nvgLineTo(vg, cx + 7.f, cy - 3.f);
        nvgLineTo(vg, cx + 3.f, cy);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + 7.f, cy + 2.f);
        nvgBezierTo(vg, cx + 6.f, cy + 7.f, cx - 4.f, cy + 7.f, cx - 6.f, cy + 3.f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 3.f, cy + 6.f);
        nvgLineTo(vg, cx - 7.f, cy + 3.f);
        nvgLineTo(vg, cx - 3.f, cy);
        nvgStroke(vg);
    }
    nvgRestore(vg);
}

void drawEditToolIcon(NVGcontext* vg, const rack::math::Rect& rect,
                      const std::shared_ptr<rack::window::Svg>& icon) {
    if (!icon) return;
    constexpr float iconSize = 20.f;
    const auto sourceSize = icon->getSize();
    const float scale = iconSize / std::max(1.f, std::max(sourceSize.x, sourceSize.y));
    nvgSave(vg);
    nvgTranslate(vg,
        rect.pos.x + (rect.size.x - sourceSize.x * scale) * 0.5f,
        rect.pos.y + (rect.size.y - sourceSize.y * scale) * 0.5f);
    nvgScale(vg, scale, scale);
    icon->draw(vg);
    nvgRestore(vg);
}

std::optional<std::string> prompt(const char* title, const std::string& initial) {
    char* result = osdialog_prompt(OSDIALOG_INFO, title, initial.c_str());
    if (!result) return std::nullopt;
    std::string value(result);
    std::free(result);
    return value;
}

int64_t ticksPerBeat(const VocalScore& score) {
    if (score.beatUnit <= 0) return kTicksPerQuarter;
    return std::max<int64_t>(1, kTicksPerQuarter * 4 / score.beatUnit);
}

std::string formatMusicalPosition(const VocalScore& score, int64_t tick) {
    tick = std::max<int64_t>(0, tick);
    const int64_t beatTicks = ticksPerBeat(score);
    const int64_t barTicks = beatTicks * std::max(1, score.beatsPerBar);
    const int64_t bar = tick / barTicks + 1;
    const int64_t withinBar = tick % barTicks;
    const int64_t beat = withinBar / beatTicks + 1;
    const int64_t remainder = withinBar % beatTicks;
    std::ostringstream result;
    result << bar << ':' << beat;
    if (remainder) result << '+' << remainder;
    return result.str();
}

std::optional<int64_t> parseMusicalPosition(const VocalScore& score, const std::string& value) {
    const size_t colon = value.find(':');
    if (colon == std::string::npos) return std::nullopt;
    const size_t plus = value.find('+', colon + 1);
    try {
        size_t used = 0;
        const int64_t bar = std::stoll(value.substr(0, colon), &used);
        if (used != colon || bar < 1) return std::nullopt;
        const std::string beatText = value.substr(colon + 1,
            plus == std::string::npos ? std::string::npos : plus - colon - 1);
        const int64_t beat = std::stoll(beatText, &used);
        if (used != beatText.size() || beat < 1 || beat > std::max(1, score.beatsPerBar))
            return std::nullopt;
        int64_t remainder = 0;
        if (plus != std::string::npos) {
            const std::string remainderText = value.substr(plus + 1);
            remainder = std::stoll(remainderText, &used);
            if (used != remainderText.size() || remainder < 0 || remainder >= ticksPerBeat(score))
                return std::nullopt;
        }
        const int64_t beatTicks = ticksPerBeat(score);
        return (bar - 1) * beatTicks * std::max(1, score.beatsPerBar) + (beat - 1) * beatTicks + remainder;
    } catch (...) {
        return std::nullopt;
    }
}

const char* actionLabel(int action) {
    switch (action) {
        case 0: return "NEW";
        case 1: return "CLEAR";
        case 2: return "IMPORT";
        case 3: return "SINGER";
        case 4: return "PHONEMIZER";
        case 5: return "TEMPO / METER";
        case 6: return "SNAP";
        case 7: return "JUMP";
        case 8: return "NOTES";
        case 9: return "PITCH CURVE";
        case 10: return "DYNAMICS";
        case 11: return "LYRIC";
        case 12: return "PHONEMES";
        case 13: return "VIBRATO";
        case 14: return "SECTION FROM NOTES";
        case 15: return "RANGE";
        case 16: return "RENAME";
        case 17: return "DELETE";
        case 18: return "UNDO";
        case 19: return "REDO";
        case 20: return "FIT SONG";
        case 21: return "FIT SECTION";
        case 22: return "CLOSE";
        case 23: return "INSERT LYRIC";
        case 24: return "SUSTAINED VOWEL";
        case 25: return "TRIGGERED WORD";
        case 26: return "PHRASE + REST";
        case 27: return "PHONEME TIMING";
        case 28: return "PLAY / PAUSE";
        case 29: return "RESET";
        case 30: return "LOOP";
        case 31: return "SONG / SECTION";
        case 32: return "FILE";
        case 33: return "EDIT";
        case 35: return "RESTORE VOICE SHAPING";
        case 36: return "FOLLOW";
        case 37: return "VIEW";
        case 38: return "TONE";
        case 39: return "TIMING";
        case 40: return "SELECT";
        case 41: return "DRAW NOTE";
        case 42: return "ERASE NOTE";
        case 43: return "SLICE NOTE";
        default: return "";
    }
}

const char* actionHelp(int action) {
    switch (action) {
        case 0: return "Replace the score with the six-note Adachi Rei first-sound phrase";
        case 1: return "Remove every note and section; Undo restores the score";
        case 2: return "Import one vocal track from OpenUtau USTX, UTAU UST, or MIDI";
        case 3: return "Choose bundled Adachi Rei or validate an external UTAU voicebank";
        case 4: return "Switch between the singer's recommended English mode and Japanese Auto";
        case 5: return "Set the score's nominal BPM and global time signature";
        case 6: return "Choose a musical snap division, or turn Snap Off for free tick-level timing";
        case 7: return "Move the editor view to an exact tick or bar";
        case 8: return "Edit note position, pitch, duration, selection, and phoneme timing";
        case 9: return "Draw and drag the selected note's authored pitch curve in cents";
        case 10: return "Draw and drag the selected note's authored loudness curve in dB";
        case 11: return "Edit English words/X-SAMPA hints or Japanese kana/romaji inline; Return commits and Tab advances";
        case 12: return "Resolved phones; edit with | separators (romaji is accepted for Japanese banks)";
        case 13: return "Set vibrato start, depth in cents, and rate in Hz";
        case 14: return "Create a named section spanning the selected notes";
        case 15: return "Set the active section start and end as musical bar:beat positions";
        case 16: return "Rename the active section";
        case 17: return "Delete the active section without deleting its notes";
        case 18: return "Undo the previous score edit";
        case 19: return "Redo the previously undone score edit";
        case 20: return "Fit the complete score, including first-phoneme preroll, in view";
        case 21: return "Fit the active section in view";
        case 22: return "Close the score editor and return to the Rack patch";
        case 23: return "Click a gap to add a lyric note, or split an existing note";
        case 24: return "Create a loop-ready sustained vowel instrument score";
        case 25: return "Create a short, armed one-shot word for the TRIG input";
        case 26: return "Create a loopable phrase section followed by a musical rest";
        case 27: return "Enter exact phoneme position, preutterance, overlap, attack, and release";
        case 28: return "Start or pause VocalRack's internal transport; a patched RUN gate takes priority";
        case 29: return "Return the playhead to the active song or section start";
        case 30: return "Repeat the active playback range instead of stopping at its end";
        case 31: return "Use the whole song, or the selected section's editable start and end bounds";
        case 32: return "Create a score or import UTAU, OpenUtau, or MIDI files";
        case 33: return "Undo, redo, copy, delete, create sections, or restore selected notes";
        case 35: return "Clear selected notes' alias, pitch, dynamics, vibrato, and phoneme timing; preserve lyrics and notes";
        case 36: return "Keep the moving playhead visible; manual timeline navigation turns this off";
        case 37: return "Fit the song, section, or selection and control playhead following";
        case 38: return "Edit the selected note's MIDI tone";
        case 39: return "Edit the selected note's musical start and duration";
        case 40: return "Select, move, and resize existing notes (V)";
        case 41: return "Draw a note into empty monophonic time (D)";
        case 42: return "Delete a clicked note; Undo restores it (E)";
        case 43: return "Split a clicked note at the snap point with a vowel-continuation second half (S)";
        default: return "";
    }
}

bool isBlackKey(int midi) {
    const int pitchClass = ((midi % 12) + 12) % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

std::string noteName(int midi) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[((midi % 12) + 12) % 12]) + std::to_string(midi / 12 - 1);
}

std::optional<int> parseMidiTone(const std::string& value) {
    try {
        size_t used = 0;
        const int numeric = std::stoi(value, &used);
        if (used == value.size() && numeric >= 0 && numeric <= 127) return numeric;
    } catch (...) {
    }

    if (value.size() < 2) return std::nullopt;
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    static const int natural[] = {9, 11, 0, 2, 4, 5, 7};  // A through G
    if (letter < 'A' || letter > 'G') return std::nullopt;
    size_t octaveOffset = 1;
    int pitchClass = natural[letter - 'A'];
    if (value.size() > 2 && (value[1] == '#' || value[1] == 'b' || value[1] == 'B')) {
        pitchClass += value[1] == '#' ? 1 : -1;
        octaveOffset = 2;
    }
    try {
        size_t used = 0;
        const int octave = std::stoi(value.substr(octaveOffset), &used);
        if (used != value.size() - octaveOffset) return std::nullopt;
        const int midi = (octave + 1) * 12 + pitchClass;
        if (midi < 0 || midi > 127) return std::nullopt;
        return midi;
    } catch (...) {
        return std::nullopt;
    }
}

std::string snapLabel(int64_t ticks) {
    switch (ticks) {
        case 1920: return "1/1";
        case 960: return "1/2";
        case 480: return "1/4";
        case 240: return "1/8";
        case 160: return "1/8T";
        case 120: return "1/16";
        case 80: return "1/16T";
        case 60: return "1/32";
        default: return std::to_string(ticks) + "t";
    }
}

}  // namespace

struct InlineLyricField : rack::ui::TextField {
    std::function<void(bool, int)> onFinish;
    bool closing = false;
    bool invalid = false;

    void finish(bool commit, int advance = 0) {
        if (closing) return;
        closing = true;
        if (onFinish) onFinish(commit, advance);
    }

    void onSelectKey(const SelectKeyEvent& e) override {
        if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            finish(true);
            e.consume(this);
            return;
        }
        if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
            finish(false);
            e.consume(this);
            return;
        }
        if (e.action == GLFW_PRESS && e.key == GLFW_KEY_TAB) {
            finish(true, (e.mods & GLFW_MOD_SHIFT) ? -1 : 1);
            e.consume(this);
            return;
        }
        rack::ui::TextField::onSelectKey(e);
    }

    void onDeselect(const DeselectEvent&) override {
        finish(true);
    }

    void draw(const DrawArgs& args) override {
        const rack::math::Rect local = {{0.f, 0.f}, box.size};
        fillRect(args.vg, local,
                 invalid ? nvgRGBA(72, 19, 34, 252) : nvgRGBA(8, 13, 21, 252), 4.f);
        strokeRect(args.vg, local, invalid ? kDanger : kPink, 2.f, 4.f);
        nvgSave(args.vg);
        nvgScissor(args.vg, 5.f, 1.f,
                   std::max(1.f, box.size.x - (invalid ? 30.f : 10.f)),
                   std::max(1.f, box.size.y - 2.f));
        const std::string shown = text.empty() ? placeholder : text;
        const NVGcolor color = text.empty() ? kTextMuted : kText;
        ::vocalrack::text(args.vg, 9.f, box.size.y * 0.5f, 13.f, color, shown);
        if (APP->event->getSelectedWidget() == this) {
            float bounds[4]{};
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgFontSize(args.vg, 13.f * kEditorTextScale);
            const size_t caretByte = static_cast<size_t>(std::clamp(cursor, 0, static_cast<int>(text.size())));
            const std::string beforeCaret = text.substr(0, caretByte);
            nvgTextBounds(args.vg, 9.f, box.size.y * 0.5f, beforeCaret.c_str(), nullptr, bounds);
            const float caret = std::clamp(bounds[2] + 2.f, 9.f, box.size.x - 7.f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, caret, 6.f);
            nvgLineTo(args.vg, caret, box.size.y - 6.f);
            nvgStrokeColor(args.vg, kYellow);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStroke(args.vg);
        }
        nvgRestore(args.vg);
        if (invalid)
            ::vocalrack::text(args.vg, box.size.x - 11.f, box.size.y * 0.5f,
                              13.f, kDanger, "!", NVG_ALIGN_CENTER);
        Widget::draw(args);
    }
};

struct InspectorValueField : rack::ui::TextField {
    std::function<void(bool, int)> onFinish;
    bool closing = false;
    bool invalid = false;

    void finish(bool commit, int advance = 0) {
        if (closing) return;
        closing = true;
        if (onFinish) onFinish(commit, advance);
    }

    void onSelectKey(const SelectKeyEvent& e) override {
        if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            finish(true);
            e.consume(this);
            return;
        }
        if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
            finish(false);
            e.consume(this);
            return;
        }
        if (e.action == GLFW_PRESS && e.key == GLFW_KEY_TAB) {
            finish(true, (e.mods & GLFW_MOD_SHIFT) ? -1 : 1);
            e.consume(this);
            return;
        }
        rack::ui::TextField::onSelectKey(e);
    }

    void onDeselect(const DeselectEvent&) override {
        finish(true);
    }

    void draw(const DrawArgs& args) override {
        const rack::math::Rect local = {{0.f, 0.f}, box.size};
        fillRect(args.vg, local,
                 invalid ? nvgRGBA(72, 19, 34, 252) : nvgRGBA(8, 13, 21, 252), 3.f);
        strokeRect(args.vg, local, invalid ? kDanger : kPink, 1.6f, 3.f);
        nvgSave(args.vg);
        nvgScissor(args.vg, 4.f, 1.f,
                   std::max(1.f, box.size.x - (invalid ? 25.f : 8.f)),
                   std::max(1.f, box.size.y - 2.f));
        const std::string shown = text.empty() ? placeholder : text;
        ::vocalrack::text(args.vg, 7.f, box.size.y * 0.5f, 10.f,
                          text.empty() ? kTextMuted : kText, shown);
        if (APP->event->getSelectedWidget() == this) {
            // Our themed field replaces Rack's stock TextField drawing, so it
            // must also draw the editing caret itself. Use the real cursor
            // position (rather than the end of the string) so arrow keys and
            // mouse placement have visible, conventional feedback.
            const size_t caretByte = static_cast<size_t>(std::clamp(cursor, 0, static_cast<int>(text.size())));
            const std::string beforeCaret = text.substr(0, caretByte);
            float bounds[4]{};
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgFontSize(args.vg, 10.f * kEditorTextScale);
            nvgTextBounds(args.vg, 7.f, box.size.y * 0.5f, beforeCaret.c_str(), nullptr, bounds);
            const float caret = std::clamp(bounds[2] + 1.5f, 7.f, box.size.x - 6.f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, caret, 5.f);
            nvgLineTo(args.vg, caret, box.size.y - 5.f);
            nvgStrokeColor(args.vg, kYellow);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStroke(args.vg);
        }
        nvgRestore(args.vg);
        if (invalid)
            ::vocalrack::text(args.vg, box.size.x - 9.f, box.size.y * 0.5f,
                              11.f, kDanger, "!", NVG_ALIGN_CENTER);
        Widget::draw(args);
    }
};

// The inspector is a dedicated interaction surface rather than painted
// pseudo-buttons. Keeping it as a child layer guarantees that clicks and
// slider drags are routed to the property panel even when Rack's fullscreen
// canvas has focus. Inline TextFields are added later and naturally sit above
// this transparent layer while they are active.
struct InspectorInteractionLayer : rack::widget::OpaqueWidget {
    std::function<bool(rack::math::Vec)> onPress;
    std::function<void(rack::math::Vec)> onDrag;
    std::function<void()> onEnd;

    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && onPress) {
            const rack::math::Vec parentPos = e.pos.plus(box.pos);
            if (onPress(parentPos)) {
                e.consume(this);
                return;
            }
        }
        OpaqueWidget::onButton(e);
    }

    void onDragMove(const DragMoveEvent& e) override {
        if (onDrag) onDrag(e.mouseDelta);
    }

    void onDragEnd(const DragEndEvent&) override {
        if (onEnd) onEnd();
    }
};

VocalEditor::VocalEditor(VocalModule* module) : module_(module) {
    horizontalResizeCursor_ = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    handCursor_ = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    textCursor_ = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    crosshairCursor_ = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
    static constexpr const char* kToolIconFiles[] = {
        "res/icons/mouse-pointer-2.svg", "res/icons/pencil.svg",
        "res/icons/eraser.svg", "res/icons/scissors.svg",
    };
    for (size_t i = 0; i < editToolIcons_.size(); ++i)
        editToolIcons_[i] = APP->window->loadSvg(asset::plugin(pluginInstance, kToolIconFiles[i]));
    box.size = APP->scene->box.size;
    if (const char* visualTooltip = std::getenv("VOCALRACK_EDITOR_TOOLTIP_ACTION")) {
        hoveredAction_ = std::clamp(std::atoi(visualTooltip), 0, 43);
        visualTooltipForced_ = true;
    }
    if (const char* visualInline = std::getenv("VOCALRACK_EDITOR_INLINE_LYRIC_NOTE"))
        pendingInlineLyricNote_ = std::max(0, std::atoi(visualInline));
    if (module_ && !module_->score.notes.empty()) {
        const double playhead = module_->displayPlayheadTick.load();
        auto found = std::find_if(module_->score.notes.begin(), module_->score.notes.end(),
            [&](const Note& note) { return playhead >= note.startTick && playhead < note.endTick(); });
        selection_.insert(found == module_->score.notes.end() ? 0
            : static_cast<size_t>(found - module_->score.notes.begin()));
    }
    auto* inspectorLayer = new InspectorInteractionLayer;
    inspectorInteractionLayer_ = inspectorLayer;
    inspectorLayer->onPress = [this](rack::math::Vec pos) { return handleInspectorPress(pos); };
    inspectorLayer->onDrag = [this](rack::math::Vec delta) { handleInspectorDrag(delta); };
    inspectorLayer->onEnd = [this] { handleInspectorDragEnd(); };
    addChild(inspectorLayer);
    gOpenEditor = this;
}

VocalEditor::~VocalEditor() {
    if (module_) module_->endAudition();
    if (APP && APP->window && APP->window->win) glfwSetCursor(APP->window->win, nullptr);
    if (horizontalResizeCursor_) glfwDestroyCursor(horizontalResizeCursor_);
    if (handCursor_) glfwDestroyCursor(handCursor_);
    if (textCursor_) glfwDestroyCursor(textCursor_);
    if (crosshairCursor_) glfwDestroyCursor(crosshairCursor_);
    if (gOpenEditor == this) gOpenEditor = nullptr;
}

void VocalEditor::layout() {
    const float margin = 12.f;
    const float headerHeight = 46.f;
    const float toolbarHeight = 52.f;
    // Two dedicated help rows keep mode-specific instructions and navigation
    // shortcuts readable at the 1024 px laptop viewport used by Rack's E2E.
    const float footerHeight = 45.f;
    const float scrollBarHeight = 14.f;
    window_ = {{margin, margin}, {std::max(616.f, box.size.x - margin * 2.f), std::max(456.f, box.size.y - margin * 2.f)}};
    toolbar_ = {{window_.pos.x, window_.pos.y + headerHeight}, {window_.size.x, toolbarHeight}};

    const float contentTop = toolbar_.pos.y + toolbar_.size.y;
    const float contentBottom = window_.pos.y + window_.size.y - footerHeight;
    const float laneContentBottom = contentBottom - scrollBarHeight;
    const float sidebarWidth = std::clamp(window_.size.x * 0.17f, 190.f, 220.f);
    const float inspectorWidth = std::clamp(window_.size.x * 0.21f, 240.f, 280.f);
    sidebar_ = {{window_.pos.x, contentTop}, {sidebarWidth, contentBottom - contentTop}};
    inspector_ = {{window_.pos.x + window_.size.x - inspectorWidth, contentTop},
                  {inspectorWidth, contentBottom - contentTop}};

    const float labelWidth = 74.f;
    const float editorLeft = sidebar_.pos.x + sidebar_.size.x;
    const float mainLeft = editorLeft + labelWidth;
    const float mainWidth = inspector_.pos.x - mainLeft;
    const float rulerHeight = 25.f;
    const float sectionHeight = 24.f;
    const float available = laneContentBottom - contentTop - rulerHeight - sectionHeight;
    const float lowerTotal = std::clamp(available * 0.38f, 180.f, 240.f);
    const float phonemeHeight = std::clamp(lowerTotal * 0.30f, 52.f, 72.f);
    const float expressionHeight = (lowerTotal - phonemeHeight) * 0.5f;
    const float pianoHeight = std::max(80.f, available - lowerTotal);

    ruler_ = {{mainLeft, contentTop}, {mainWidth, rulerHeight}};
    sectionLane_ = {{mainLeft, ruler_.pos.y + ruler_.size.y}, {mainWidth, sectionHeight}};
    keyboard_ = {{editorLeft, sectionLane_.pos.y + sectionLane_.size.y}, {labelWidth, pianoHeight}};
    piano_ = {{mainLeft, keyboard_.pos.y}, {mainWidth, pianoHeight}};
    phonemeLane_ = {{mainLeft, piano_.pos.y + piano_.size.y}, {mainWidth, phonemeHeight}};
    pitchLane_ = {{mainLeft, phonemeLane_.pos.y + phonemeLane_.size.y}, {mainWidth, expressionHeight}};
    dynamicsLane_ = {{mainLeft, pitchLane_.pos.y + pitchLane_.size.y}, {mainWidth, expressionHeight}};
    timelineScrollBar_ = {{mainLeft, dynamicsLane_.pos.y + dynamicsLane_.size.y}, {mainWidth, scrollBarHeight}};
    pitchModeToggle_ = {{keyboard_.pos.x + 5.f, pitchLane_.pos.y + 4.f}, {64.f, 21.f}};
    dynamicsModeToggle_ = {{keyboard_.pos.x + 5.f, dynamicsLane_.pos.y + 4.f}, {64.f, 21.f}};

    actionHits_.clear();
    sidebarHeadings_.clear();
    const float headerY = window_.pos.y + headerHeight * 0.5f;
    actionHits_.push_back({22, {{window_.pos.x + window_.size.x - 82.f, headerY - 14.f}, {66.f, 28.f}}});

    float tx = toolbar_.pos.x + 14.f;
    const float ty = toolbar_.pos.y + 12.f;
    const float th = 28.f;
    auto addToolbar = [&](int action, float width) {
        actionHits_.push_back({action, {{tx, ty}, {width, th}}});
        tx += width + 6.f;
    };
    addToolbar(32, 48.f);
    addToolbar(33, 48.f);
    addToolbar(37, 48.f);
    tx += 8.f;
    addToolbar(40, 34.f);
    addToolbar(41, 34.f);
    addToolbar(42, 34.f);
    addToolbar(43, 34.f);
    tx += 8.f;
    addToolbar(36, 70.f);
    tx += 8.f;
    addToolbar(28, 54.f);
    addToolbar(29, 48.f);
    addToolbar(30, 44.f);
    addToolbar(31, 96.f);

    const float sx = sidebar_.pos.x + 12.f;
    const float sw = sidebar_.size.x - 24.f;
    float sy = sidebar_.pos.y + 20.f;
    auto heading = [&](const char* name) {
        sidebarHeadings_.push_back({name, sy});
        sy += 13.f;
    };
    auto row = [&](std::initializer_list<int> actions) {
        const float gap = 5.f;
        const float width = (sw - gap * (actions.size() - 1)) / actions.size();
        float x = sx;
        for (int action : actions) {
            actionHits_.push_back({action, {{x, sy}, {width, 25.f}}});
            x += width + gap;
        }
        sy += 31.f;
    };
    heading("VOICE & TIMING");
    row({3, 4});
    row({5, 6});
    row({7});
    sy += 5.f;
    heading("SECTIONS");
    row({14});
    row({15, 16, 17});
    sy += 5.f;
    heading("LYRIC FLOW");
    row({23});

    const float ix = inspector_.pos.x + 12.f;
    const float iw = inspector_.size.x - 24.f;
    inspectorControls_.clear();
    auto addTextField = [&](InspectorField field, float y) {
        const rack::math::Rect rowRect = {{ix, inspector_.pos.y + y}, {iw, 28.f}};
        const rack::math::Rect valueRect = {{ix + 82.f, rowRect.pos.y + 2.f}, {iw - 82.f, 24.f}};
        inspectorControls_.push_back({field, rowRect, valueRect, {}, false});
    };
    auto addSliderField = [&](InspectorField field, float y) {
        const rack::math::Rect rowRect = {{ix, inspector_.pos.y + y}, {iw, 24.f}};
        const float valueWidth = 68.f;
        const rack::math::Rect valueRect = {{ix + iw - valueWidth, rowRect.pos.y + 1.f}, {valueWidth, 22.f}};
        const rack::math::Rect sliderRect = {{ix + 79.f, rowRect.pos.y + 6.f},
                                             {std::max(28.f, iw - 79.f - valueWidth - 7.f), 12.f}};
        inspectorControls_.push_back({field, rowRect, valueRect, sliderRect, true});
    };
    addTextField(InspectorField::Lyric, 64.f);
    addTextField(InspectorField::Alias, 96.f);
    addTextField(InspectorField::Tone, 128.f);
    addTextField(InspectorField::Start, 160.f);
    addTextField(InspectorField::Length, 192.f);
    addSliderField(InspectorField::Position, 250.f);
    addSliderField(InspectorField::Preutterance, 276.f);
    addSliderField(InspectorField::Overlap, 302.f);
    addSliderField(InspectorField::Attack, 328.f);
    addSliderField(InspectorField::Release, 354.f);
    addTextField(InspectorField::VibratoEnabled, 404.f);
    addSliderField(InspectorField::VibratoStart, 436.f);
    addSliderField(InspectorField::VibratoDepth, 462.f);
    addSliderField(InspectorField::VibratoRate, 488.f);
    actionHits_.push_back({35, {{ix, inspector_.pos.y + inspector_.size.y - 38.f}, {iw, 28.f}}});
    actionHits_.push_back({9, pitchModeToggle_});
    actionHits_.push_back({10, dynamicsModeToggle_});
}

void VocalEditor::step() {
    box.size = APP->scene->box.size;
    layout();
    if (inspectorInteractionLayer_) inspectorInteractionLayer_->box = inspector_;
    // A click is ultimately selected by Rack after ButtonEvent dispatch. If a
    // TextField is created and focused inside that same dispatch, Rack selects
    // the click target immediately afterward and the field receives Deselect,
    // appearing to do nothing. Open the real field on the following frame so
    // its focus and caret persist.
    if (pendingInspectorEditField_ != InspectorField::None && !inspectorValueField_) {
        if (--pendingInspectorEditFrames_ <= 0) {
            const InspectorField field = pendingInspectorEditField_;
            pendingInspectorEditField_ = InspectorField::None;
            beginInspectorEdit(field);
        }
    }
    // The click which creates this overlay can otherwise leave Rack's canvas
    // selected after its matching mouse-up. Reassert focus for the first few
    // live frames so Space and all other editor shortcuts work immediately.
    if (initialFocusFrames_ > 0 && !inlineLyricField_ && !inspectorValueField_) {
        --initialFocusFrames_;
        APP->event->setSelectedWidget(this);
    }
    if (!viewInitialized_) {
        viewInitialized_ = true;
        if (!module_->editorViewInitialized) {
            fitPitchRange();
            zoomFull();
            module_->editorViewInitialized = true;
        }
    }
    if (module_->editorFollowPlayhead && piano_.size.x > 1.f) {
        const float visibleTicks = piano_.size.x / std::max(0.02f, module_->editorZoomX);
        const float playhead = static_cast<float>(module_->displayPlayheadTick.load(std::memory_order_relaxed));
        const float leftSafe = module_->editorScrollX + visibleTicks * 0.12f;
        const float rightSafe = module_->editorScrollX + visibleTicks * 0.88f;
        if (playhead < leftSafe || playhead > rightSafe) {
            const float minimumScroll = -static_cast<float>(kTicksPerQuarter);
            const float maximumScroll = std::max(minimumScroll,
                static_cast<float>(module_->score.endTick() + kTicksPerQuarter) - visibleTicks);
            module_->editorScrollX = std::clamp(playhead - visibleTicks * 0.22f,
                                                 minimumScroll, maximumScroll);
        }
    }
    if (pendingInlineLyricNote_ >= 0 && pendingInlineLyricDelayFrames_ > 0) {
        --pendingInlineLyricDelayFrames_;
    } else if (pendingInlineLyricNote_ >= 0 && !module_->score.notes.empty()) {
        const size_t noteIndex = std::min<size_t>(static_cast<size_t>(pendingInlineLyricNote_),
                                                  module_->score.notes.size() - 1);
        pendingInlineLyricNote_ = -1;
        selection_.clear();
        selection_.insert(noteIndex);
        module_->panelPlaying.store(false, std::memory_order_relaxed);
        beginInlineLyric(noteIndex);
    }
    updateInlineLyricLayout();
    updateInspectorFieldLayout();
    Widget::step();
}

rack::math::Rect VocalEditor::noteRect(const Note& note) const {
    const float x = piano_.pos.x + (note.startTick - module_->editorScrollX) * module_->editorZoomX;
    const float y = piano_.pos.y + module_->editorScrollY + (84 - note.midiNote) * module_->editorZoomY;
    return {{x, y}, {std::max(3.f, note.durationTick * module_->editorZoomX), std::max(3.f, module_->editorZoomY - 1.f)}};
}

int64_t VocalEditor::xToTick(float x) const {
    return static_cast<int64_t>(std::llround((x - piano_.pos.x) / module_->editorZoomX + module_->editorScrollX));
}

int VocalEditor::yToMidi(float y) const {
    return std::clamp(static_cast<int>(std::lround(84 - (y - piano_.pos.y - module_->editorScrollY) / module_->editorZoomY)), 0, 127);
}

int64_t VocalEditor::snapTick(int64_t tick) const {
    if (!module_->editorSnapEnabled) return std::max<int64_t>(0, tick);
    const int64_t grid = std::max<int64_t>(1, module_->editorSnapTick);
    return std::max<int64_t>(0, static_cast<int64_t>(std::llround(tick / static_cast<double>(grid))) * grid);
}

size_t VocalEditor::noteAt(rack::math::Vec pos) const {
    for (size_t i = module_->score.notes.size(); i-- > 0;) {
        if (noteRect(module_->score.notes[i]).contains(pos)) return i;
    }
    return module_->score.notes.size();
}

size_t VocalEditor::noteAtTick(int64_t tick) const {
    for (size_t i = module_->score.notes.size(); i-- > 0;) {
        const auto& note = module_->score.notes[i];
        if (tick >= note.startTick && tick < note.endTick()) return i;
    }
    return module_->score.notes.size();
}

void VocalEditor::copySelectedNotes() {
    clipboard_.clear();
    for (const auto index : selection_)
        if (index < module_->score.notes.size()) clipboard_.push_back(module_->score.notes[index]);
}

void VocalEditor::deleteSelectedNotes() {
    if (selection_.empty()) return;
    const auto before = scoreToJson(module_->score);
    std::vector<Note> kept;
    kept.reserve(module_->score.notes.size() - std::min(selection_.size(), module_->score.notes.size()));
    for (size_t i = 0; i < module_->score.notes.size(); ++i)
        if (!selection_.count(i)) kept.push_back(module_->score.notes[i]);
    module_->score.notes = std::move(kept);
    selection_.clear();
    clearCurvePointSelection();
    module_->commitScoreEdit(before, "Delete notes");
}

void VocalEditor::pasteClipboardAtTick(int64_t tick) {
    if (clipboard_.empty()) return;
    const auto before = scoreToJson(module_->score);
    const int64_t first = std::min_element(clipboard_.begin(), clipboard_.end(),
        [](const Note& a, const Note& b) { return a.startTick < b.startTick; })->startTick;
    const int64_t target = snapTick(tick);
    const int64_t delta = target - first;
    std::vector<std::string> insertedIds;
    for (auto note : clipboard_) {
        note.id = makeUuid();
        note.startTick = std::max<int64_t>(0, note.startTick + delta);
        insertedIds.push_back(note.id);
        module_->score.notes.push_back(std::move(note));
    }
    module_->score.normalize();
    const auto errors = module_->score.validate();
    if (!errors.empty()) {
        module_->score = scoreFromJson(before);
        throw std::runtime_error("Cannot paste here: " + errors.front());
    }
    selection_.clear();
    for (size_t i = 0; i < module_->score.notes.size(); ++i)
        if (std::find(insertedIds.begin(), insertedIds.end(), module_->score.notes[i].id) != insertedIds.end())
            selection_.insert(i);
    module_->commitScoreEdit(before, "Paste notes");
}

void VocalEditor::addNoteAt(rack::math::Vec pos) {
    const auto before = scoreToJson(module_->score);
    Note note;
    note.id = makeUuid();
    note.startTick = snapTick(xToTick(pos.x));
    note.durationTick = module_->editorSnapTick * 2;
    note.midiNote = yToMidi(pos.y);
    note.lyric = isJapanesePhonemizer(module_->phonemizerName) ? "あ" : "a";

    int64_t nextStart = std::numeric_limits<int64_t>::max();
    for (const auto& existing : module_->score.notes) {
        if (note.startTick >= existing.startTick && note.startTick < existing.endTick())
            return;  // Ordinary monophonic collision: leave the score unchanged.
        if (existing.startTick > note.startTick) nextStart = std::min(nextStart, existing.startTick);
    }
    if (nextStart != std::numeric_limits<int64_t>::max())
        note.durationTick = std::min(note.durationTick, nextStart - note.startTick);
    if (note.durationTick < (module_->editorSnapEnabled ? module_->editorSnapTick : 1))
        throw std::runtime_error("There is not enough room at this snap grid to add a note");

    const std::string id = note.id;
    module_->score.notes.push_back(std::move(note));
    module_->score.normalize();
    const auto errors = module_->score.validate();
    if (!errors.empty()) {
        module_->score = scoreFromJson(before);
        throw std::runtime_error(errors.front());
    }
    selection_.clear();
    const auto found = std::find_if(module_->score.notes.begin(), module_->score.notes.end(),
                                    [&](const Note& candidate) { return candidate.id == id; });
    if (found != module_->score.notes.end()) selection_.insert(static_cast<size_t>(found - module_->score.notes.begin()));
    module_->commitScoreEdit(before, "Add note");
    module_->auditionMidiNote(yToMidi(pos.y));
}

void VocalEditor::beginDrawNote(rack::math::Vec pos) {
    drawBefore_ = scoreToJson(module_->score);
    drawNoteId_ = makeUuid();
    drawStart_ = pos;
    drawCurrent_ = pos;
    drawAnchorTick_ = std::max<int64_t>(0, snapTick(xToTick(pos.x)));
    drawingNote_ = true;
    updateDrawNote();

    if (scoreToJson(module_->score) != drawBefore_) {
        auditionHolding_ = true;
        auditionKeyboardDrag_ = false;
        lastDragAuditionMidi_ = yToMidi(pos.y);
        module_->beginAuditionMidiNote(lastDragAuditionMidi_);
    }
}

void VocalEditor::updateDrawNote() {
    if (!drawingNote_) return;

    // Rebuild the preview from the pre-gesture score on every move. This makes
    // one pencil gesture one undo step and avoids sending render jobs while a
    // note is still being shaped.
    module_->score = scoreFromJson(drawBefore_);
    selection_.clear();

    const int64_t minimumDuration = module_->editorSnapEnabled
        ? std::max<int64_t>(1, module_->editorSnapTick) : 1;
    const int64_t currentTick = std::max<int64_t>(0, snapTick(xToTick(drawCurrent_.x)));
    int64_t startTick = std::min(drawAnchorTick_, currentTick);
    int64_t endTick = std::max(drawAnchorTick_, currentTick);
    if (startTick == endTick) endTick = startTick + minimumDuration;

    // The score is monophonic. Clamp the live note to the free region around
    // the initial click, so dragging toward a neighbour feels bounded rather
    // than ending in an error dialog or corrupting the gesture.
    int64_t freeStart = 0;
    int64_t freeEnd = std::numeric_limits<int64_t>::max();
    for (const auto& existing : module_->score.notes) {
        if (existing.endTick() <= drawAnchorTick_)
            freeStart = std::max(freeStart, existing.endTick());
        else if (existing.startTick > drawAnchorTick_)
            freeEnd = std::min(freeEnd, existing.startTick);
    }
    startTick = std::max(startTick, freeStart);
    endTick = std::min(endTick, freeEnd);
    if (endTick - startTick < minimumDuration) {
        if (drawAnchorTick_ + minimumDuration <= freeEnd) {
            startTick = std::max(drawAnchorTick_, freeStart);
            endTick = startTick + minimumDuration;
        } else if (drawAnchorTick_ - minimumDuration >= freeStart) {
            endTick = std::min(drawAnchorTick_, freeEnd);
            startTick = endTick - minimumDuration;
        } else {
            return;
        }
    }

    Note note;
    note.id = drawNoteId_;
    note.startTick = startTick;
    note.durationTick = endTick - startTick;
    note.midiNote = yToMidi(drawCurrent_.y);
    note.lyric = isJapanesePhonemizer(module_->phonemizerName) ? "あ" : "a";
    module_->score.notes.push_back(std::move(note));
    module_->score.normalize();
    const auto found = std::find_if(module_->score.notes.begin(), module_->score.notes.end(),
        [&](const Note& candidate) { return candidate.id == drawNoteId_; });
    if (found != module_->score.notes.end())
        selection_.insert(static_cast<size_t>(found - module_->score.notes.begin()));
}

void VocalEditor::finishDrawNote() {
    if (!drawingNote_) return;
    drawingNote_ = false;
    const auto errors = module_->score.validate();
    if (!errors.empty() || scoreToJson(module_->score) == drawBefore_) {
        module_->score = scoreFromJson(drawBefore_);
        selection_.clear();
    } else {
        module_->score.normalize();
        module_->commitScoreEdit(drawBefore_, "Draw note");
    }
    drawBefore_.clear();
    drawNoteId_.clear();
}

void VocalEditor::openFileMenu() {
    auto* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("FILE"));
    menu->addChild(rack::createMenuItem("New blank score", "", [this] { toolbarAction(1); }));
    menu->addChild(rack::createSubmenuItem("New from template", "", [this](rack::ui::Menu* child) {
        child->addChild(rack::createSubmenuItem("English", "", [this](rack::ui::Menu* language) {
            language->addChild(rack::createMenuItem("First sound / phrase: we sing a star", "", [this] { toolbarAction(0); }));
            language->addChild(rack::createMenuItem("Sustained vowel instrument: a", "", [this] { toolbarAction(45); }));
            language->addChild(rack::createMenuItem("Triggered word: sing", "", [this] { toolbarAction(46); }));
            language->addChild(rack::createMenuItem("Looping phrase + one-beat rest", "", [this] { toolbarAction(47); }));
        }));
        child->addChild(rack::createSubmenuItem("Japanese", "", [this](rack::ui::Menu* language) {
            language->addChild(rack::createMenuItem("First sound / phrase: あだちれいう", "", [this] { toolbarAction(44); }));
            language->addChild(rack::createMenuItem("Sustained vowel instrument: う", "", [this] { toolbarAction(24); }));
            language->addChild(rack::createMenuItem("Triggered word: あだち", "", [this] { toolbarAction(25); }));
            language->addChild(rack::createMenuItem("Looping phrase + one-beat rest", "", [this] { toolbarAction(26); }));
        }));
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Save lossless VocalRack project...", RACK_MOD_CTRL_NAME "+S", [this] {
        saveVocalProjectFile(module_);
    }));
    menu->addChild(rack::createMenuItem("Export OpenUtau USTX...", RACK_MOD_CTRL_NAME "+Shift+S", [this] {
        exportVocalUstxFile(module_);
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Import UTAU / OpenUtau / MIDI...", "Shift+I", [this] {
        toolbarAction(2);
    }));
}

void VocalEditor::openEditMenu() {
    auto* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("EDIT"));
    menu->addChild(rack::createMenuItem("Undo", RACK_MOD_CTRL_NAME "+Z", [this] { toolbarAction(18); }));
    menu->addChild(rack::createMenuItem("Redo", RACK_MOD_CTRL_NAME "+Shift+Z", [this] { toolbarAction(19); }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Select all notes", RACK_MOD_CTRL_NAME "+A", [this] {
        selection_.clear();
        for (size_t index = 0; index < module_->score.notes.size(); ++index)
            selection_.insert(index);
        clearCurvePointSelection();
    }, module_->score.notes.empty()));
    menu->addChild(new rack::ui::MenuSeparator);
    const bool noSelection = selection_.empty();
    menu->addChild(rack::createMenuItem("Copy selected notes", RACK_MOD_CTRL_NAME "+C",
        [this] { copySelectedNotes(); }, noSelection));
    menu->addChild(rack::createMenuItem("Delete selected notes", "Delete",
        [this] { deleteSelectedNotes(); }, noSelection));
    menu->addChild(rack::createMenuItem("Create section from selected...", "",
        [this] { addSection(); }, noSelection));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Restore selected voice shaping to defaults", "",
        [this] { resetSelectedVoiceShaping(); }, noSelection));
}

void VocalEditor::openViewMenu() {
    auto* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("VIEW"));
    menu->addChild(rack::createMenuItem("Fit complete song", "", [this] { toolbarAction(20); }));
    menu->addChild(rack::createMenuItem("Fit active section", "", [this] { toolbarAction(21); },
                                         module_->score.sections.empty()));
    menu->addChild(rack::createMenuItem("Fit selected notes", "", [this] {
        module_->editorFollowPlayhead = false;
        zoomSelection();
    }, selection_.empty()));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Follow playhead", module_->editorFollowPlayhead ? "On" : "Off",
                                         [this] { toolbarAction(36); }));
}

void VocalEditor::openSnapMenu() {
    auto* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("SNAP"));
    menu->addChild(rack::createMenuItem("Off: free timing", module_->editorSnapEnabled ? "" : "Selected", [this] {
        module_->editorSnapEnabled = false;
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    auto addDivision = [this, menu](const char* name, int64_t ticks) {
        menu->addChild(rack::createMenuItem(name,
            module_->editorSnapEnabled && module_->editorSnapTick == ticks ? "Selected" : "", [this, ticks] {
                module_->editorSnapTick = ticks;
                module_->editorSnapEnabled = true;
            }));
    };
    addDivision("Quarter note  (1/4)", 480);
    addDivision("Eighth note  (1/8)", 240);
    addDivision("Eighth-note triplet  (1/8T)", 160);
    addDivision("Sixteenth note  (1/16)", 120);
    addDivision("Sixteenth-note triplet  (1/16T)", 80);
    addDivision("Thirty-second note  (1/32)", 60);
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Custom tick division...", "", [this] {
        const auto value = prompt("Custom snap in ticks (480 = quarter note)",
                                  std::to_string(module_->editorSnapTick));
        if (!value || value->empty()) return;
        try {
            size_t used = 0;
            const int64_t ticks = std::stoll(*value, &used);
            if (used != value->size()) throw std::invalid_argument("trailing text");
            module_->editorSnapTick = std::clamp<int64_t>(ticks, 1, 1920);
            module_->editorSnapEnabled = true;
        } catch (...) {
            osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, "Snap must be a whole number from 1 to 1920 ticks");
        }
    }));
}

void VocalEditor::openContextMenu(rack::math::Vec pos) {
    auto* menu = rack::createMenu();

    size_t hit = module_->score.notes.size();
    if (piano_.contains(pos)) hit = noteAt(pos);
    else if (phonemeLane_.contains(pos) || pitchLane_.contains(pos) || dynamicsLane_.contains(pos))
        hit = noteAtTick(xToTick(pos.x));

    if (sectionLane_.contains(pos)) {
        const int64_t tick = xToTick(pos.x);
        size_t sectionIndex = module_->score.sections.size();
        for (size_t i = 0; i < module_->score.sections.size(); ++i) {
            const auto& section = module_->score.sections[i];
            if (tick >= section.startTick && tick < section.endTick) { sectionIndex = i; break; }
        }
        if (sectionIndex < module_->score.sections.size()) {
            module_->params[VocalModule::SECTION_PARAM].setValue(static_cast<float>(sectionIndex));
            menu->addChild(rack::createMenuLabel("SECTION  " + module_->score.sections[sectionIndex].name));
            menu->addChild(rack::createMenuItem("Fit section", "", [this] {
                module_->editorFollowPlayhead = false;
                zoomSection();
            }));
            menu->addChild(rack::createMenuItem("Rename...", "", [this] { renameSection(); }));
            menu->addChild(rack::createMenuItem("Edit musical range...", "", [this] { editSectionBounds(); }));
            menu->addChild(new rack::ui::MenuSeparator);
            menu->addChild(rack::createMenuItem("Delete section", "", [this] { deleteSection(); }));
            return;
        }
    }

    if (hit < module_->score.notes.size()) {
        if (!selection_.count(hit)) {
            selection_.clear();
            selection_.insert(hit);
        }
        const std::string title = "NOTE  " + module_->score.notes[hit].lyric + "  |  " + noteName(module_->score.notes[hit].midiNote);
        menu->addChild(rack::createMenuLabel(title));
        menu->addChild(rack::createMenuItem("Edit lyric on note", "", [this, hit] { beginInlineLyric(hit); }));
        menu->addChild(rack::createMenuItem("Edit lyric in inspector", "", [this] {
            beginInspectorEdit(InspectorField::Lyric);
        }));
        menu->addChild(rack::createMenuItem("Edit phoneme / alias in inspector", "", [this] {
            beginInspectorEdit(InspectorField::Alias);
        }));
        menu->addChild(rack::createMenuItem("Edit tone in inspector", "", [this] {
            beginInspectorEdit(InspectorField::Tone);
        }));
        menu->addChild(rack::createMenuItem("Edit start in inspector", "", [this] {
            beginInspectorEdit(InspectorField::Start);
        }));
        menu->addChild(rack::createMenuItem("Edit length in inspector", "", [this] {
            beginInspectorEdit(InspectorField::Length);
        }));
        menu->addChild(rack::createSubmenuItem("Phoneme timing", "", [this](rack::ui::Menu* child) {
            child->addChild(rack::createMenuItem("Position offset", "", [this] {
                beginInspectorEdit(InspectorField::Position);
            }));
            child->addChild(rack::createMenuItem("Preutterance", "", [this] {
                beginInspectorEdit(InspectorField::Preutterance);
            }));
            child->addChild(rack::createMenuItem("Overlap", "", [this] {
                beginInspectorEdit(InspectorField::Overlap);
            }));
            child->addChild(rack::createMenuItem("Attack", "", [this] {
                beginInspectorEdit(InspectorField::Attack);
            }));
            child->addChild(rack::createMenuItem("Release", "", [this] {
                beginInspectorEdit(InspectorField::Release);
            }));
            child->addChild(rack::createMenuItem("Reset to voicebank timing", "", [this] { resetPhonemeTiming(); }));
        }));
        menu->addChild(rack::createSubmenuItem("Vibrato", "", [this](rack::ui::Menu* child) {
            child->addChild(rack::createMenuItem("Start", "", [this] {
                beginInspectorEdit(InspectorField::VibratoStart);
            }));
            child->addChild(rack::createMenuItem("Depth", "", [this] {
                beginInspectorEdit(InspectorField::VibratoDepth);
            }));
            child->addChild(rack::createMenuItem("Rate", "", [this] {
                beginInspectorEdit(InspectorField::VibratoRate);
            }));
        }));
        menu->addChild(rack::createSubmenuItem("Continue previous vowel", "", [this, hit](rack::ui::Menu* child) {
            auto apply = [this, hit](const std::string& lyric) {
                if (hit >= module_->score.notes.size()) return;
                const auto before = scoreToJson(module_->score);
                module_->score.notes[hit].lyric = lyric;
                module_->score.notes[hit].aliasOverride.reset();
                module_->score.notes[hit].phonemeOverrides.clear();
                module_->commitScoreEdit(before, "Set vowel continuation");
            };
            child->addChild(rack::createMenuItem("OpenUtau continuation", "+", [apply] { apply("+"); }));
            child->addChild(rack::createMenuItem("Japanese long vowel", "ー", [apply] { apply("ー"); }));
            child->addChild(rack::createMenuItem("Legacy continuation", "-", [apply] { apply("-"); }));
        }));
        menu->addChild(rack::createMenuItem("Restore voice shaping to singer defaults", "", [this] {
            resetSelectedVoiceShaping();
        }));
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuItem("Fit selected notes", "", [this] {
            module_->editorFollowPlayhead = false;
            zoomSelection();
        }));
        menu->addChild(rack::createMenuItem("Edit pitch curve", "", [this] { mode_ = EditMode::Pitch; clearCurvePointSelection(); }));
        menu->addChild(rack::createMenuItem("Edit dynamics", "", [this] { mode_ = EditMode::Dynamics; clearCurvePointSelection(); }));
        menu->addChild(rack::createCheckMenuItem(
            "Connect pitch from previous touching note", "OpenUtau snap_first",
            [this, hit] {
                return hit < module_->score.notes.size() &&
                    module_->score.notes[hit].pitchSnapFirst;
            },
            [this, hit] {
                if (hit >= module_->score.notes.size()) return;
                const auto before = scoreToJson(module_->score);
                const bool enabled = !module_->score.notes[hit].pitchSnapFirst;
                for (const auto index : selection_)
                    if (index < module_->score.notes.size())
                        module_->score.notes[index].pitchSnapFirst = enabled;
                module_->commitScoreEdit(before,
                    enabled ? "Connect note pitch" : "Disconnect note pitch");
            }));
        if (pitchLane_.contains(pos))
            menu->addChild(rack::createMenuItem("Add pitch point here", "", [this, pos] { mode_ = EditMode::Pitch; addCurvePoint(pos); }));
        if (dynamicsLane_.contains(pos))
            menu->addChild(rack::createMenuItem("Add dynamics point here", "", [this, pos] { mode_ = EditMode::Dynamics; addCurvePoint(pos); }));
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuItem("Copy selected", RACK_MOD_CTRL_NAME "+C", [this] { copySelectedNotes(); }));
        menu->addChild(rack::createMenuItem("Slice at cursor", "", [this, hit, pos] {
            sliceNoteAt(hit, xToTick(pos.x));
        }));
        menu->addChild(rack::createMenuItem("Create section from selected...", "", [this] { addSection(); }));
        menu->addChild(rack::createMenuItem("Delete selected", "Del", [this] { deleteSelectedNotes(); }));
        return;
    }

    menu->addChild(rack::createMenuLabel("SCORE"));
    if (piano_.contains(pos)) {
        menu->addChild(rack::createMenuItem("Add note here", "", [this, pos] {
            try { addNoteAt(pos); }
            catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
        }));
        menu->addChild(rack::createMenuItem("Insert lyric here...", "", [this, pos] {
            try { insertLyricAt(pos); }
            catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
        }));
        menu->addChild(rack::createMenuItem("Paste at cursor", "", [this, pos] {
            try { pasteClipboardAtTick(xToTick(pos.x)); }
            catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
        }, clipboard_.empty()));
        menu->addChild(new rack::ui::MenuSeparator);
    }
    menu->addChild(rack::createMenuItem("Fit song", "", [this] {
        module_->editorFollowPlayhead = false;
        zoomFull();
    }));
    menu->addChild(rack::createMenuItem("New score", "", [this] { toolbarAction(0); }));
    menu->addChild(rack::createMenuItem("Import UTAU / OpenUtau / MIDI...", "", [this] { importUstx(); }));
}

void VocalEditor::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    fillRect(vg, {{0.f, 0.f}, box.size}, kBackdrop);
    fillRect(vg, window_, kWindow, 8.f);
    strokeRect(vg, window_, nvgRGB(91, 104, 127), 1.2f, 8.f);
    fillRect(vg, {{window_.pos.x, window_.pos.y}, {window_.size.x, 4.f}}, kPink, 3.f);

    const float headerCenter = window_.pos.y + 25.f;
    text(vg, window_.pos.x + 18.f, headerCenter - 6.f, 17.f, kPink, "VOCAL  /  SCORE EDITOR");
    text(vg, window_.pos.x + 18.f, headerCenter + 12.f, 11.f, kTextMuted,
         module_->score.title + "  |  " + std::to_string(module_->score.notes.size()) + " notes  |  " +
             std::to_string(selection_.size()) + " selected");
    text(vg, window_.pos.x + window_.size.x - 100.f, headerCenter, 11.f, kTextMuted,
         module_->singerDisplayName() + "  |  " + module_->statusText(), NVG_ALIGN_RIGHT);

    fillRect(vg, toolbar_, nvgRGB(15, 20, 28));
    strokeRect(vg, toolbar_, nvgRGB(48, 58, 74));
    fillRect(vg, sidebar_, nvgRGB(17, 22, 31));
    strokeRect(vg, sidebar_, nvgRGB(48, 58, 74));
    fillRect(vg, inspector_, nvgRGB(17, 22, 31));
    strokeRect(vg, inspector_, nvgRGB(48, 58, 74));

    auto selectedAction = [&](int action) {
        return (action >= 8 && action <= 10 && static_cast<int>(mode_) == action - 8) ||
               (action >= 40 && action <= 43 && mode_ == EditMode::Notes &&
                    static_cast<int>(noteTool_) == action - 40) ||
               (action == 23 && insertingLyric_) ||
               (action == 28 && module_->panelPlaying.load(std::memory_order_relaxed)) ||
               (action == 30 && module_->params[VocalModule::LOOP_PARAM].getValue() >= 0.5f) ||
               (action == 31 && module_->params[VocalModule::RANGE_PARAM].getValue() >= 0.5f) ||
               (action == 36 && module_->editorFollowPlayhead);
    };
    const bool hasSelectedNote = !selection_.empty() && *selection_.begin() < module_->score.notes.size();
    auto inspectorLabel = [&](int action) -> std::string {
        if (!hasSelectedNote) {
            switch (action) {
                case 11: return "LYRIC    -";
                case 12: return "PHONEMES    -";
                case 38: return "TONE    -";
                case 39: return "TIMING    -";
                case 27: return "PHONEME TIMING    -";
                case 13: return "VIBRATO    -";
                default: return actionLabel(action);
            }
        }
        const auto& note = module_->score.notes[*selection_.begin()];
        if (action == 11) return "LYRIC    " + note.lyric;
        if (action == 12) return "PHONEMES    " + inspectorValue(InspectorField::Alias);
        if (action == 38) return "TONE    " + noteName(note.midiNote) + "  (" + std::to_string(note.midiNote) + ")";
        if (action == 39) {
            std::ostringstream value;
            value << "TIMING    " << formatMusicalPosition(module_->score, note.startTick) << "  |  "
                  << std::fixed << std::setprecision(2)
                  << note.durationTick / static_cast<double>(ticksPerBeat(module_->score)) << " beats";
            return value.str();
        }
        if (action == 27) {
            const auto& timing = note.phonemeTiming;
            const bool custom = timing.positionOffsetTick || timing.preutteranceDeltaMs || timing.overlapDeltaMs ||
                                timing.attackTimeDeltaMs || timing.releaseTimeDeltaMs;
            return std::string("PHONEME TIMING    ") + (custom ? "CUSTOM" : "SINGER DEFAULTS");
        }
        if (action == 13) {
            if (note.vibrato.depthCents <= 0.f) return "VIBRATO    OFF";
            std::ostringstream value;
            value << "VIBRATO    " << std::lround(note.vibrato.depthCents) << " cents  @  "
                  << std::fixed << std::setprecision(1) << note.vibrato.rateHz << " Hz";
            return value.str();
        }
        return actionLabel(action);
    };
    for (const auto& hit : actionHits_) {
        if (hit.action == 9 || hit.action == 10) continue;
        const bool selected = selectedAction(hit.action);
        const bool close = hit.action == 22;
        const bool inspectorAction = inspector_.contains(hit.rect.getCenter());
        const bool inspectorDisabled = inspectorAction && !hasSelectedNote;
        const NVGcolor background = selected ? nvgRGB(82, 35, 62) : close ? nvgRGB(48, 30, 40) :
                                    inspectorDisabled ? nvgRGB(25, 30, 39) : kSurfaceRaised;
        const NVGcolor border = selected || close ? kPink : kBorder;
        fillRect(vg, hit.rect, background, 4.f);
        strokeRect(vg, hit.rect, border, selected ? 1.5f : 1.f, 4.f);
        if (hit.action >= 28 && hit.action <= 30) {
            drawTransportIcon(vg, hit.rect, hit.action,
                              module_->panelPlaying.load(std::memory_order_relaxed),
                              selected ? nvgRGB(255, 225, 236) : kText);
        } else if (hit.action >= 40 && hit.action <= 43) {
            drawEditToolIcon(vg, hit.rect, editToolIcons_[static_cast<size_t>(hit.action - 40)]);
        } else {
            std::string label = inspectorAction ? inspectorLabel(hit.action) : actionLabel(hit.action);
            if (hit.action == 6)
                label = module_->editorSnapEnabled ? "SNAP: " + snapLabel(module_->editorSnapTick) : "SNAP: OFF";
            if (hit.action == 31)
                label = module_->params[VocalModule::RANGE_PARAM].getValue() >= 0.5f ? "RANGE: SECTION" : "RANGE: SONG";
            else if (hit.action == 32 || hit.action == 33 || hit.action == 37)
                label += "  ▾";
            const float fontSize = hit.rect.size.x < 62.f ? 8.2f : hit.rect.size.x < 78.f ? 9.f : 9.6f;
            text(vg, inspectorAction ? hit.rect.pos.x + 10.f : hit.rect.pos.x + hit.rect.size.x * 0.5f,
                 hit.rect.pos.y + hit.rect.size.y * 0.5f, fontSize,
                 inspectorDisabled ? nvgRGB(105, 115, 132) : selected ? nvgRGB(255, 225, 236) : kText,
                 label, inspectorAction ? NVG_ALIGN_LEFT : NVG_ALIGN_CENTER);
        }
    }

    for (const auto& heading : sidebarHeadings_)
        text(vg, sidebar_.pos.x + 12.f, heading.second, 9.f, kTextMuted, heading.first);

    text(vg, inspector_.pos.x + 12.f, inspector_.pos.y + 17.f, 10.f, kTextMuted, "NOTE INSPECTOR");
    if (hasSelectedNote) {
        const auto& note = module_->score.notes[*selection_.begin()];
        text(vg, inspector_.pos.x + 12.f, inspector_.pos.y + 38.f, 12.f, kText,
             note.lyric + "  |  " + noteName(note.midiNote) + "  |  " +
                 std::to_string(note.durationTick) + " ticks");
    } else {
        text(vg, inspector_.pos.x + 12.f, inspector_.pos.y + 38.f, 11.f, kTextMuted,
             "Click a lyric block to inspect its note");
    }
    text(vg, inspector_.pos.x + 12.f, inspector_.pos.y + 56.f, 8.5f, kTextMuted, "BASIC");
    text(vg, inspector_.pos.x + 12.f, inspector_.pos.y + 235.f, 8.5f, kTextMuted, "PHONEME TIMING");
    text(vg, inspector_.pos.x + 12.f, inspector_.pos.y + 393.f, 8.5f, kTextMuted, "VIBRATO");

    auto fieldLabel = [](InspectorField field) -> const char* {
        switch (field) {
            case InspectorField::Lyric: return "LYRIC";
            case InspectorField::Alias: return "PHONEMES";
            case InspectorField::Tone: return "TONE";
            case InspectorField::Start: return "START";
            case InspectorField::Length: return "LENGTH";
            case InspectorField::Position: return "POSITION";
            case InspectorField::Preutterance: return "PREUTTER";
            case InspectorField::Overlap: return "OVERLAP";
            case InspectorField::Attack: return "ATTACK";
            case InspectorField::Release: return "RELEASE";
            case InspectorField::VibratoEnabled: return "ENABLED";
            case InspectorField::VibratoStart: return "START";
            case InspectorField::VibratoDepth: return "DEPTH";
            case InspectorField::VibratoRate: return "RATE";
            default: return "";
        }
    };
    auto fieldUnit = [](InspectorField field) -> const char* {
        switch (field) {
            case InspectorField::Length: return "beats";
            case InspectorField::Position: return "t";
            case InspectorField::Preutterance:
            case InspectorField::Overlap:
            case InspectorField::Attack:
            case InspectorField::Release: return "ms";
            case InspectorField::VibratoStart: return "%";
            case InspectorField::VibratoDepth: return "c";
            case InspectorField::VibratoRate: return "Hz";
            default: return "";
        }
    };
    for (const auto& control : inspectorControls_) {
        const bool vibratoOff = hasSelectedNote &&
            module_->score.notes[*selection_.begin()].vibrato.depthCents <= 0.f;
        const bool secondaryDisabled = vibratoOff &&
            (control.field == InspectorField::VibratoStart || control.field == InspectorField::VibratoRate);
        const NVGcolor labelColor = !hasSelectedNote || secondaryDisabled ? nvgRGB(105, 115, 132) : kTextMuted;
        text(vg, control.row.pos.x, control.row.pos.y + control.row.size.y * 0.5f,
             8.2f, labelColor, fieldLabel(control.field));
        if (control.field == InspectorField::VibratoEnabled) {
            const bool on = hasSelectedNote && !vibratoOff;
            const rack::math::Rect toggle = {{control.value.pos.x, control.value.pos.y + 3.f}, {38.f, 18.f}};
            fillRect(vg, toggle, on ? nvgRGB(105, 51, 81) : nvgRGB(36, 44, 57), 9.f);
            strokeRect(vg, toggle, on ? kPink : kBorder, 1.f, 9.f);
            nvgBeginPath(vg);
            nvgCircle(vg, on ? toggle.pos.x + 29.f : toggle.pos.x + 9.f,
                      toggle.pos.y + toggle.size.y * 0.5f, 6.f);
            nvgFillColor(vg, on ? kPink : kTextMuted);
            nvgFill(vg);
            text(vg, toggle.pos.x + 47.f, toggle.pos.y + toggle.size.y * 0.5f, 8.5f,
                 on ? kText : kTextMuted, on ? "ON" : "OFF");
            continue;
        }

        std::string shown = hasSelectedNote ? inspectorValue(control.field) : "-";
        if (control.field == InspectorField::Alias && shown.empty()) shown = "AUTO (rendering...)";
        if (control.field == InspectorField::Tone && hasSelectedNote)
            shown += "  (" + std::to_string(module_->score.notes[*selection_.begin()].midiNote) + ")";
        if (!control.hasSlider) {
            fillRect(vg, control.value, nvgRGB(24, 31, 42), 3.f);
            strokeRect(vg, control.value,
                       inspectorEditField_ == control.field ? kPink : kBorder,
                       inspectorEditField_ == control.field ? 1.5f : 1.f, 3.f);
            text(vg, control.value.pos.x + 7.f, control.value.pos.y + control.value.size.y * 0.5f,
                 9.f, hasSelectedNote ? kText : nvgRGB(105, 115, 132), shown);
            if (control.field == InspectorField::Length && hasSelectedNote)
                text(vg, control.value.pos.x + control.value.size.x - 6.f,
                     control.value.pos.y + control.value.size.y * 0.5f,
                     7.5f, kTextMuted, fieldUnit(control.field), NVG_ALIGN_RIGHT);
            continue;
        }

        const auto range = inspectorRange(control.field);
        const float numeric = inspectorNumericValue(control.field);
        const float normalized = std::clamp((numeric - range.first) /
                                             std::max(0.0001f, range.second - range.first), 0.f, 1.f);
        const float trackY = control.slider.pos.y + control.slider.size.y * 0.5f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, control.slider.pos.x, trackY);
        nvgLineTo(vg, control.slider.pos.x + control.slider.size.x, trackY);
        nvgStrokeColor(vg, secondaryDisabled ? nvgRGB(55, 63, 78) : kBorder);
        nvgStrokeWidth(vg, 3.f);
        nvgStroke(vg);
        const float knobX = control.slider.pos.x + normalized * control.slider.size.x;
        nvgBeginPath(vg);
        nvgCircle(vg, knobX, trackY, 4.5f);
        nvgFillColor(vg, secondaryDisabled ? nvgRGB(90, 99, 115) : kPink);
        nvgFill(vg);
        fillRect(vg, control.value, nvgRGB(24, 31, 42), 3.f);
        strokeRect(vg, control.value,
                   inspectorEditField_ == control.field ? kPink : kBorder,
                   inspectorEditField_ == control.field ? 1.5f : 1.f, 3.f);
        const std::string valueWithUnit = shown + (hasSelectedNote ? std::string(" ") + fieldUnit(control.field) : "");
        text(vg, control.value.pos.x + control.value.size.x * 0.5f,
             control.value.pos.y + control.value.size.y * 0.5f,
             7.7f, hasSelectedNote ? kText : nvgRGB(105, 115, 132), valueWithUnit, NVG_ALIGN_CENTER);
    }
    if (!inspectorError_.empty())
        text(vg, inspector_.pos.x + 12.f, inspector_.pos.y + inspector_.size.y - 51.f,
             7.8f, kDanger, inspectorError_);

    const float infoY = sidebar_.pos.y + sidebar_.size.y - 61.f;
    if (infoY > sidebarHeadings_.back().second + 55.f) {
        nvgSave(vg);
        nvgScissor(vg, sidebar_.pos.x, sidebar_.pos.y, sidebar_.size.x, sidebar_.size.y);
        strokeRect(vg, {{sidebar_.pos.x + 12.f, infoY - 9.f}, {sidebar_.size.x - 24.f, 1.f}}, nvgRGB(48, 58, 74));
        std::ostringstream timing;
        timing << static_cast<int>(std::lround(module_->score.nominalBpm)) << " BPM  |  "
               << module_->score.beatsPerBar << "/" << module_->score.beatUnit << "  |  "
               << (module_->editorSnapEnabled ? std::to_string(module_->editorSnapTick) + " tick snap" : "snap off");
        text(vg, sidebar_.pos.x + 12.f, infoY + 5.f, 10.f, kTextMuted, timing.str());
        text(vg, sidebar_.pos.x + 12.f, infoY + 21.f, 10.f, kTextMuted, module_->phonemizerName);
        if (!selection_.empty() && *selection_.begin() < module_->score.notes.size()) {
            const auto& note = module_->score.notes[*selection_.begin()];
            text(vg, sidebar_.pos.x + 12.f, infoY + 39.f, 11.f, kText,
                 noteName(note.midiNote) + "  |  " + note.lyric + "  |  " + std::to_string(note.durationTick) + " ticks");
        } else {
            textBox(vg, sidebar_.pos.x + 12.f, infoY + 30.f, sidebar_.size.x - 24.f,
                    9.2f, kTextMuted, "Select a note to edit its voice shaping");
        }
        nvgRestore(vg);
    }

    const rack::math::Rect editorLabels = {{keyboard_.pos.x, ruler_.pos.y},
                                            {keyboard_.size.x, timelineScrollBar_.pos.y + timelineScrollBar_.size.y - ruler_.pos.y}};
    fillRect(vg, editorLabels, nvgRGB(20, 26, 35));
    strokeRect(vg, editorLabels, nvgRGB(48, 58, 74));
    fillRect(vg, ruler_, nvgRGB(25, 32, 43));
    fillRect(vg, sectionLane_, nvgRGB(28, 35, 46));
    fillRect(vg, piano_, nvgRGB(17, 22, 31));
    fillRect(vg, phonemeLane_, nvgRGB(22, 28, 38));
    fillRect(vg, pitchLane_, nvgRGB(18, 24, 33));
    fillRect(vg, dynamicsLane_, nvgRGB(22, 27, 37));

    text(vg, keyboard_.pos.x + keyboard_.size.x * 0.5f, ruler_.pos.y + ruler_.size.y * 0.5f, 9.f, kTextMuted, "TIMELINE", NVG_ALIGN_CENTER);
    text(vg, keyboard_.pos.x + 8.f, sectionLane_.pos.y + sectionLane_.size.y * 0.5f, 9.f, kTextMuted, "SECTIONS");
    text(vg, keyboard_.pos.x + 7.f, phonemeLane_.pos.y + 19.f, 9.f, kCyan, "PHONEME");
    text(vg, keyboard_.pos.x + 7.f, phonemeLane_.pos.y + 35.f, 8.f, kTextMuted, "timing");
    auto drawLaneToggle = [&](const rack::math::Rect& rect, const char* label, NVGcolor accent, bool selected) {
        fillRect(vg, rect, selected ? nvgRGBA(82, 35, 62, 255) : kSurfaceRaised, 4.f);
        strokeRect(vg, rect, selected ? accent : kBorder, selected ? 1.5f : 1.f, 4.f);
        text(vg, rect.pos.x + rect.size.x * 0.5f, rect.pos.y + rect.size.y * 0.5f,
             8.2f, selected ? accent : kText, label, NVG_ALIGN_CENTER);
    };
    drawLaneToggle(pitchModeToggle_, "PITCH EDIT", kYellow, mode_ == EditMode::Pitch);
    drawLaneToggle(dynamicsModeToggle_, "DYN EDIT", kPink, mode_ == EditMode::Dynamics);
    text(vg, keyboard_.pos.x + 8.f, pitchModeToggle_.pos.y + 31.f, 8.f, kTextMuted, "cents");
    text(vg, keyboard_.pos.x + 8.f, dynamicsModeToggle_.pos.y + 31.f, 8.f, kTextMuted, "dB gain");
    text(vg, keyboard_.pos.x + 8.f, timelineScrollBar_.pos.y + timelineScrollBar_.size.y * 0.5f,
         8.f, kTextMuted, "VIEW");

    auto drawVerticalGrid = [&](const rack::math::Rect& rect) {
        const int64_t firstTick = std::max<int64_t>(0, xToTick(rect.pos.x));
        const int64_t lastTick = xToTick(rect.pos.x + rect.size.x);
        const int64_t guideGrid = std::max<int64_t>(1, module_->editorSnapTick);
        const int64_t firstGrid = (firstTick / guideGrid) * guideGrid;
        for (int64_t tick = firstGrid; tick <= lastTick; tick += guideGrid) {
            const float x = piano_.pos.x + (tick - module_->editorScrollX) * module_->editorZoomX;
            const bool bar = tick % (kTicksPerQuarter * std::max(1, module_->score.beatsPerBar)) == 0;
            const bool beat = tick % kTicksPerQuarter == 0;
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, rect.pos.y);
            nvgLineTo(vg, x, rect.pos.y + rect.size.y);
            nvgStrokeColor(vg, bar ? nvgRGBA(155, 176, 211, 115) : beat ? kGridStrong : kGrid);
            nvgStrokeWidth(vg, bar ? 1.4f : beat ? 1.f : 0.6f);
            nvgStroke(vg);
        }
    };

    drawVerticalGrid(ruler_);
    drawVerticalGrid(sectionLane_);
    drawVerticalGrid(piano_);
    drawVerticalGrid(phonemeLane_);
    drawVerticalGrid(pitchLane_);
    drawVerticalGrid(dynamicsLane_);

    nvgSave(vg);
    nvgScissor(vg, ruler_.pos.x, ruler_.pos.y, ruler_.size.x, ruler_.size.y);
    const int64_t barTicks = kTicksPerQuarter * std::max(1, module_->score.beatsPerBar);
    const int64_t firstBar = std::max<int64_t>(0, xToTick(ruler_.pos.x) / barTicks);
    const int64_t lastBar = std::max<int64_t>(firstBar, xToTick(ruler_.pos.x + ruler_.size.x) / barTicks + 1);
    for (int64_t bar = firstBar; bar <= lastBar; ++bar) {
        const float x = piano_.pos.x + (bar * barTicks - module_->editorScrollX) * module_->editorZoomX;
        text(vg, x + 5.f, ruler_.pos.y + ruler_.size.y * 0.5f, 9.f, kTextMuted, "BAR " + std::to_string(bar + 1));
    }
    nvgRestore(vg);

    nvgSave(vg);
    nvgScissor(vg, sectionLane_.pos.x, sectionLane_.pos.y, sectionLane_.size.x, sectionLane_.size.y);
    for (size_t i = 0; i < module_->score.sections.size(); ++i) {
        const auto& section = module_->score.sections[i];
        const float x = piano_.pos.x + (section.startTick - module_->editorScrollX) * module_->editorZoomX;
        const float end = piano_.pos.x + (section.endTick - module_->editorScrollX) * module_->editorZoomX;
        const bool active = static_cast<size_t>(std::max(0, static_cast<int>(std::lround(module_->params[VocalModule::SECTION_PARAM].getValue())))) == i;
        rack::math::Rect sectionRect = {{x, sectionLane_.pos.y + 4.f}, {std::max(3.f, end - x), sectionLane_.size.y - 8.f}};
        fillRect(vg, sectionRect, active ? nvgRGBA(245, 104, 152, 130) : nvgRGBA(80, 198, 218, 75), 3.f);
        text(vg, x + 5.f, sectionLane_.pos.y + sectionLane_.size.y * 0.5f, 9.f, kText, section.name);
    }
    nvgRestore(vg);

    nvgSave(vg);
    nvgScissor(vg, keyboard_.pos.x, keyboard_.pos.y, keyboard_.size.x, keyboard_.size.y);
    for (int midi = 0; midi <= 127; ++midi) {
        const float y = piano_.pos.y + module_->editorScrollY + (84 - midi) * module_->editorZoomY;
        const rack::math::Rect keyRect = {{keyboard_.pos.x, y}, {keyboard_.size.x, module_->editorZoomY}};
        fillRect(vg, keyRect, isBlackKey(midi) ? nvgRGB(29, 35, 46) : nvgRGB(47, 55, 68));
        strokeRect(vg, keyRect, nvgRGBA(7, 10, 15, 110), 0.6f);
        if (midi % 12 == 0 && module_->editorZoomY >= 8.f)
            text(vg, keyboard_.pos.x + keyboard_.size.x - 5.f, y + module_->editorZoomY * 0.5f, 8.f, kTextMuted, noteName(midi), NVG_ALIGN_RIGHT);
    }
    nvgRestore(vg);

    nvgSave(vg);
    nvgScissor(vg, piano_.pos.x, piano_.pos.y, piano_.size.x, piano_.size.y);
    for (int midi = 0; midi <= 127; ++midi) {
        const float y = piano_.pos.y + module_->editorScrollY + (84 - midi) * module_->editorZoomY;
        if (y + module_->editorZoomY < piano_.pos.y || y > piano_.pos.y + piano_.size.y) continue;
        if (isBlackKey(midi)) fillRect(vg, {{piano_.pos.x, y}, {piano_.size.x, module_->editorZoomY}}, nvgRGBA(5, 8, 13, 52));
        nvgBeginPath(vg);
        nvgMoveTo(vg, piano_.pos.x, y);
        nvgLineTo(vg, piano_.pos.x + piano_.size.x, y);
        nvgStrokeColor(vg, midi % 12 == 0 ? nvgRGBA(100, 119, 149, 75) : nvgRGBA(89, 103, 126, 28));
        nvgStrokeWidth(vg, midi % 12 == 0 ? 1.f : 0.6f);
        nvgStroke(vg);
    }

    const auto diagnostics = module_->renderSlot->copyDiagnostics();
    // Paint all note bodies first. The pitch contour needs a
    // separate pass: a transition is one performed path, not one path per
    // note. Drawing per-note contours made the outgoing baseline continue to
    // the note edge while the incoming curve branched away from it.
    for (size_t i = 0; i < module_->score.notes.size(); ++i) {
        const auto& note = module_->score.notes[i];
        const auto rect = noteRect(note);
        bool missing = false;
        for (const auto& phone : diagnostics.phonemes) {
            if (phone.sourceNoteId == note.id) {
                missing = !phone.oto;
                break;
            }
        }
        const bool selected = selection_.count(i) != 0;
        fillRect(vg, rect, missing ? kDanger : selected ? kPink : nvgRGB(43, 151, 181), 3.f);
        strokeRect(vg, rect, selected ? nvgRGB(255, 222, 234) : nvgRGBA(255, 255, 255, 65), selected ? 1.5f : 0.8f, 3.f);
    }

    // Draw exactly one performed-pitch path for each rest-delimited phrase.
    // performedAbsoluteMidi() hands ownership to the incoming note at the
    // start of its negative-time portamento, so the outgoing baseline itself
    // bends toward the next note in the same way as OpenUtau. There is no
    // baseline offshoot under the bend, and a real gap starts a new path.
    for (size_t phraseStart = 0; phraseStart < module_->score.notes.size();) {
        size_t phraseEnd = phraseStart;
        while (phraseEnd + 1 < module_->score.notes.size() &&
               module_->score.notes[phraseEnd].endTick() ==
                   module_->score.notes[phraseEnd + 1].startTick) {
            ++phraseEnd;
        }
        const int64_t contourStart = module_->score.notes[phraseStart].startTick;
        const int64_t contourEnd = std::max<int64_t>(
            contourStart + 1, module_->score.notes[phraseEnd].endTick());
        const float contourPixels = (contourEnd - contourStart) * module_->editorZoomX;
        const int contourSteps = std::clamp(
            static_cast<int>(std::ceil(contourPixels / 4.f)), 2, 1400);
        bool phraseSelected = false;
        for (size_t i = phraseStart; i <= phraseEnd; ++i)
            phraseSelected = phraseSelected || selection_.count(i) != 0;
        nvgBeginPath(vg);
        for (int step = 0; step <= contourSteps; ++step) {
            const double alpha = step / static_cast<double>(contourSteps);
            const int64_t absoluteTick = contourStart + static_cast<int64_t>(std::llround(
                (contourEnd - contourStart) * alpha));
            const float x = piano_.pos.x +
                (absoluteTick - module_->editorScrollX) * module_->editorZoomX;
            const float absoluteTone = performedAbsoluteMidi(
                module_->score, absoluteTick, module_->score.nominalBpm);
            const float y = piano_.pos.y + module_->editorScrollY +
                (84.f - absoluteTone) * module_->editorZoomY + module_->editorZoomY * 0.5f;
            if (step == 0) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
        }
        nvgStrokeColor(vg, phraseSelected ? kYellow : nvgRGBA(255, 220, 84, 190));
        nvgStrokeWidth(vg, phraseSelected ? 2.f : 1.25f);
        nvgStroke(vg);
        phraseStart = phraseEnd + 1;
    }

    // Labels and edit affordances stay above the contour.
    for (size_t i = 0; i < module_->score.notes.size(); ++i) {
        const auto& note = module_->score.notes[i];
        const auto rect = noteRect(note);
        const bool selected = selection_.count(i) != 0;
        // Keep the lyric above the contour for readability.
        if (rect.size.x > 12.f && rect.size.y >= 7.f)
            text(vg, rect.pos.x + 5.f, rect.pos.y + rect.size.y * 0.5f,
                 std::min(14.f, rect.size.y * 0.76f), kText, note.lyric);
        if (selected && inlineLyricNoteIndex_ != i && rect.size.x > 72.f && rect.size.y >= 18.f) {
            // Leave a clear resize grip at the note's right edge. The badge
            // used to overlap that grip, so clicking to shorten a wide note
            // opened lyric editing instead of starting a resize.
            const rack::math::Rect editBadge = {{rect.pos.x + rect.size.x - 44.f, rect.pos.y + 3.f},
                                                {34.f, rect.size.y - 6.f}};
            fillRect(vg, editBadge, nvgRGBA(7, 12, 20, 185), 3.f);
            strokeRect(vg, editBadge, nvgRGBA(255, 255, 255, 90), 0.8f, 3.f);
            text(vg, editBadge.pos.x + editBadge.size.x * 0.5f,
                 editBadge.pos.y + editBadge.size.y * 0.5f, 8.f, kText, "EDIT", NVG_ALIGN_CENTER);
        }
    }
    if (marqueeDragging_) {
        const float left = std::min(marqueeStart_.x, marqueeCurrent_.x);
        const float top = std::min(marqueeStart_.y, marqueeCurrent_.y);
        const rack::math::Rect marquee = {{left, top},
            {std::abs(marqueeCurrent_.x - marqueeStart_.x),
             std::abs(marqueeCurrent_.y - marqueeStart_.y)}};
        fillRect(vg, marquee, nvgRGBA(80, 198, 218, 42), 2.f);
        strokeRect(vg, marquee, nvgRGBA(144, 232, 247, 230), 1.2f, 2.f);
    }
    const float playX = piano_.pos.x + (module_->displayPlayheadTick.load() - module_->editorScrollX) * module_->editorZoomX;
    nvgBeginPath(vg);
    nvgMoveTo(vg, playX, piano_.pos.y);
    nvgLineTo(vg, playX, dynamicsLane_.pos.y + dynamicsLane_.size.y);
    nvgStrokeColor(vg, nvgRGB(255, 255, 255));
    nvgStrokeWidth(vg, 1.4f);
    nvgStroke(vg);
    nvgRestore(vg);

    nvgSave(vg);
    nvgScissor(vg, phonemeLane_.pos.x, phonemeLane_.pos.y, phonemeLane_.size.x, phonemeLane_.size.y);
    const double timingTicksPerMs = module_->score.nominalBpm * kTicksPerQuarter / 60000.0;
    for (size_t i = 0; i < module_->score.notes.size(); ++i) {
        const auto& note = module_->score.notes[i];
        std::vector<const PhonemeEvent*> notePhones;
        for (const auto& phone : diagnostics.phonemes) {
            if (phone.sourceNoteId == note.id) notePhones.push_back(&phone);
        }
        std::stable_sort(notePhones.begin(), notePhones.end(), [](const auto* left, const auto* right) {
            return left->relativeTick < right->relativeTick;
        });
        const bool selected = selection_.count(i) != 0;
        if (notePhones.empty()) {
            const int64_t positionTick = note.startTick + note.phonemeTiming.positionOffsetTick.value_or(0);
            const float positionX = piano_.pos.x +
                (positionTick - module_->editorScrollX) * module_->editorZoomX;
            const float authoredEndX = piano_.pos.x +
                (note.endTick() - module_->editorScrollX) * module_->editorZoomX;
            const rack::math::Rect rect = {{positionX + 1.f, phonemeLane_.pos.y + 7.f},
                {std::max(2.f, authoredEndX - positionX - 2.f), phonemeLane_.size.y - 14.f}};
            fillRect(vg, rect, nvgRGBA(80, 198, 218, 55), 3.f);
            if (rect.size.x > 18.f)
                text(vg, rect.pos.x + 5.f, rect.pos.y + rect.size.y * 0.5f,
                     10.f, kTextMuted, "pending");
            continue;
        }

        // A word can resolve to several phone events. Draw every one at its
        // actual renderer position; the old first-match loop made medial
        // phones such as `star`'s た disappear from the editor even though the
        // audio engine rendered them.
        for (size_t phoneIndex = 0; phoneIndex < notePhones.size(); ++phoneIndex) {
            const auto& phone = *notePhones[phoneIndex];
            const bool firstPhone = phoneIndex == 0;
            const bool lastPhone = phoneIndex + 1 == notePhones.size();
            std::string alias = phone.selectedAlias.empty() ? phone.requestedAlias : phone.selectedAlias;
            const bool missing = !phone.oto;
            if (missing) alias = "MISSING: " + alias;
            const int64_t positionTick = phone.relativeTick +
                (firstPhone ? note.phonemeTiming.positionOffsetTick.value_or(0) : 0);
            const int64_t eventEndTick = !lastPhone
                ? std::max<int64_t>(positionTick + 1, notePhones[phoneIndex + 1]->relativeTick)
                : note.endTick();
            const float positionX = piano_.pos.x +
                (positionTick - module_->editorScrollX) * module_->editorZoomX;
            const float eventEndX = piano_.pos.x +
                (eventEndTick - module_->editorScrollX) * module_->editorZoomX;
            if (!phone.oto) {
                const rack::math::Rect rect = {{positionX + 1.f, phonemeLane_.pos.y + 7.f},
                    {std::max(2.f, eventEndX - positionX - 2.f), phonemeLane_.size.y - 14.f}};
                fillRect(vg, rect, nvgRGBA(217, 77, 93, 160), 3.f);
                if (rect.size.x > 18.f)
                    text(vg, rect.pos.x + 5.f, rect.pos.y + rect.size.y * 0.5f,
                         10.f, kText, alias);
                continue;
            }

            const float preutterMs = std::clamp(static_cast<float>(phone.oto->preutterMs) +
                (firstPhone ? note.phonemeTiming.preutteranceDeltaMs.value_or(0.f) : 0.f),
                0.f, 500.f);
            const float overlapMs = std::clamp(static_cast<float>(phone.oto->overlapMs) +
                (firstPhone ? note.phonemeTiming.overlapDeltaMs.value_or(0.f) : 0.f),
                -500.f, preutterMs);
            const float fadeInMs = std::clamp((overlapMs > 0.f ? overlapMs : 5.f) +
                (firstPhone ? note.phonemeTiming.attackTimeDeltaMs.value_or(0.f) : 0.f),
                0.f, 500.f);
            float envelopeEndTick = static_cast<float>(eventEndTick);
            float naturalFadeOutMs = 35.f;
            const PhonemeEvent* nextPhone = !lastPhone ? notePhones[phoneIndex + 1] : nullptr;
            const Note* nextPhoneNote = &note;
            bool nextIsFirst = false;
            if (!nextPhone && i + 1 < module_->score.notes.size() &&
                module_->score.notes[i + 1].startTick == note.endTick()) {
                nextPhoneNote = &module_->score.notes[i + 1];
                const auto found = std::find_if(diagnostics.phonemes.begin(), diagnostics.phonemes.end(),
                    [&](const PhonemeEvent& candidate) {
                        return candidate.sourceNoteId == nextPhoneNote->id;
                    });
                if (found != diagnostics.phonemes.end()) {
                    nextPhone = &*found;
                    nextIsFirst = true;
                }
            }
            if (nextPhone && nextPhone->oto) {
                const float nextPreutter = std::clamp(static_cast<float>(nextPhone->oto->preutterMs) +
                    (nextIsFirst ? nextPhoneNote->phonemeTiming.preutteranceDeltaMs.value_or(0.f) : 0.f),
                    0.f, 500.f);
                const float nextOverlap = std::clamp(static_cast<float>(nextPhone->oto->overlapMs) +
                    (nextIsFirst ? nextPhoneNote->phonemeTiming.overlapDeltaMs.value_or(0.f) : 0.f),
                    -500.f, nextPreutter);
                const int64_t nextPosition = nextPhone->relativeTick +
                    (nextIsFirst ? nextPhoneNote->phonemeTiming.positionOffsetTick.value_or(0) : 0);
                envelopeEndTick = static_cast<float>(nextPosition +
                    (nextOverlap - nextPreutter) * timingTicksPerMs);
                naturalFadeOutMs = nextOverlap > 0.f ? nextOverlap : 35.f;
            }
            const float fadeOutMs = std::clamp(naturalFadeOutMs +
                (lastPhone ? note.phonemeTiming.releaseTimeDeltaMs.value_or(0.f) : 0.f),
                0.f, 500.f);
            const float tickPixels = static_cast<float>(timingTicksPerMs * module_->editorZoomX);
            const float endX = piano_.pos.x +
                (envelopeEndTick - module_->editorScrollX) * module_->editorZoomX;
            const float startX = positionX - preutterMs * tickPixels;
            const float overlapX = startX + overlapMs * tickPixels;
            const float attackX = startX + fadeInMs * tickPixels;
            const float releaseX = std::max(positionX, endX - fadeOutMs * tickPixels);
            const float top = phonemeLane_.pos.y + 9.f;
            const float bottom = phonemeLane_.pos.y + phonemeLane_.size.y - 9.f;
            const float upper = top + 8.f;
            const NVGcolor envelopeColor = selected ? kCyan : nvgRGBA(80, 198, 218, 150);
            const float visualSpan = std::max(0.f, endX - startX);
            const float minimumSlope = std::min(12.f, visualSpan * 0.28f);
            float visualAttackX = std::max(attackX, startX + minimumSlope);
            float visualReleaseX = std::min(releaseX, endX - minimumSlope);
            if (visualAttackX > visualReleaseX) {
                const float midpoint = (startX + endX) * 0.5f;
                visualAttackX = midpoint;
                visualReleaseX = midpoint;
            }
            nvgBeginPath(vg);
            nvgMoveTo(vg, startX, bottom);
            nvgLineTo(vg, visualAttackX, upper);
            nvgLineTo(vg, visualReleaseX, upper);
            nvgLineTo(vg, endX, bottom);
            nvgClosePath(vg);
            nvgFillColor(vg, selected ? nvgRGBA(80, 198, 218, 75) : nvgRGBA(80, 198, 218, 35));
            nvgFill(vg);
            nvgStrokeColor(vg, envelopeColor);
            nvgStrokeWidth(vg, selected ? 1.7f : 1.f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, positionX, top);
            nvgLineTo(vg, positionX, bottom);
            nvgStrokeColor(vg, selected ? nvgRGBA(255, 255, 255, 210) : nvgRGBA(255, 255, 255, 80));
            nvgStrokeWidth(vg, selected ? 1.5f : 0.8f);
            nvgStroke(vg);
            if (eventEndX - positionX > 18.f)
                text(vg, positionX + 6.f, phonemeLane_.pos.y +
                    phonemeLane_.size.y * 0.5f, 10.f, kText, alias);

            if (selected && firstPhone) {
                auto handle = [&](float x, float y, NVGcolor color, bool customized) {
                    nvgBeginPath(vg); nvgCircle(vg, x, y, 6.5f);
                    nvgFillColor(vg, customized ? color : kSurface); nvgFill(vg);
                    nvgStrokeColor(vg, color); nvgStrokeWidth(vg, 2.f); nvgStroke(vg);
                };
                const float startY = bottom - 4.f;
                const float overlapY = top + 4.f;
                handle(startX, startY, kCyan, note.phonemeTiming.preutteranceDeltaMs.has_value());
                handle(overlapX, overlapY, kPink, note.phonemeTiming.overlapDeltaMs.has_value());
                text(vg, startX + 10.f, startY, 9.f, kCyan,
                     "START " + std::to_string(static_cast<int>(std::lround(preutterMs))) + " ms");
                text(vg, overlapX + 10.f, overlapY, 9.f, kPink,
                     "XFADE " + std::to_string(static_cast<int>(std::lround(overlapMs))) + " ms");
            }
        }
    }
    nvgRestore(vg);

    auto drawHorizontalGuide = [&](const rack::math::Rect& lane, float normalizedY, const std::string& label, NVGcolor color) {
        const float y = lane.pos.y + lane.size.y * normalizedY;
        nvgBeginPath(vg);
        nvgMoveTo(vg, lane.pos.x, y);
        nvgLineTo(vg, lane.pos.x + lane.size.x, y);
        nvgStrokeColor(vg, nvgRGBA(132, 150, 179, normalizedY == 0.5f ? 95 : 43));
        nvgStrokeWidth(vg, normalizedY == 0.5f ? 1.f : 0.7f);
        nvgStroke(vg);
        // Keep endpoint labels inside their own lane. Drawing every label above
        // its guide made the pitch minimum and dynamics maximum collide at the
        // shared boundary on compact displays.
        const float labelY = normalizedY <= 0.f ? lane.pos.y + 10.f
                           : normalizedY >= 1.f ? lane.pos.y + lane.size.y - 10.f
                                                : y + 9.f;
        text(vg, lane.pos.x + 5.f, labelY, 8.f, color, label);
    };
    drawHorizontalGuide(pitchLane_, 0.f, "+200 cents", kYellow);
    drawHorizontalGuide(pitchLane_, 0.5f, "0 cents", kYellow);
    drawHorizontalGuide(pitchLane_, 1.f, "-200 cents", kYellow);
    drawHorizontalGuide(dynamicsLane_, 0.f, "+12 dB", kPink);
    drawHorizontalGuide(dynamicsLane_, 1.f / 3.f, "0 dB", kPink);
    drawHorizontalGuide(dynamicsLane_, 1.f, "-24 dB", kPink);

    auto drawCurveLane = [&](const rack::math::Rect& lane, bool pitch) {
        nvgSave(vg);
        nvgScissor(vg, lane.pos.x, lane.pos.y, lane.size.x, lane.size.y);
        for (size_t i = 0; i < module_->score.notes.size(); ++i) {
            const auto& note = module_->score.notes[i];
            const auto& points = pitch ? note.pitchCents.points : note.dynamicsDb.points;
            if (points.empty()) continue;
            const float noteX = piano_.pos.x + (note.startTick - module_->editorScrollX) * module_->editorZoomX;
            const bool selected = selection_.count(i) != 0;
            if (selected) fillRect(vg, {{noteX, lane.pos.y}, {note.durationTick * module_->editorZoomX, lane.size.y}},
                                   pitch ? nvgRGBA(255, 220, 84, 18) : nvgRGBA(245, 104, 152, 18));
            nvgBeginPath(vg);
            bool first = true;
            for (const auto& point : points) {
                const float x = noteX + point.tickOffset * module_->editorZoomX;
                float y;
                if (pitch) y = lane.pos.y + lane.size.y * (0.5f - std::clamp(point.value, -200.f, 200.f) / 400.f);
                else y = lane.pos.y + lane.size.y * (12.f - std::clamp(point.value, -24.f, 12.f)) / 36.f;
                if (first) nvgMoveTo(vg, x, y); else nvgLineTo(vg, x, y);
                first = false;
            }
            nvgStrokeColor(vg, pitch ? (selected ? kYellow : nvgRGBA(255, 220, 84, 140))
                                      : (selected ? kPink : nvgRGBA(245, 104, 152, 140)));
            nvgStrokeWidth(vg, selected ? 2.f : 1.2f);
            nvgStroke(vg);
            if (selected) {
                for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
                    const auto& point = points[pointIndex];
                    const float x = noteX + point.tickOffset * module_->editorZoomX;
                    const float y = pitch ? lane.pos.y + lane.size.y * (0.5f - std::clamp(point.value, -200.f, 200.f) / 400.f)
                                          : lane.pos.y + lane.size.y * (12.f - std::clamp(point.value, -24.f, 12.f)) / 36.f;
                    nvgBeginPath(vg);
                    const bool pointSelected = curveNoteIndex_ == i && curvePointIndex_ == pointIndex &&
                                               curvePointPitch_ == pitch;
                    nvgCircle(vg, x, y, pointSelected ? 5.f : 3.5f);
                    nvgFillColor(vg, pitch ? kYellow : kPink);
                    nvgFill(vg);
                    if (pointSelected) {
                        nvgBeginPath(vg);
                        nvgCircle(vg, x, y, 6.5f);
                        nvgStrokeColor(vg, kText);
                        nvgStrokeWidth(vg, 1.5f);
                        nvgStroke(vg);
                    }
                }
            }
        }
        nvgRestore(vg);
    };
    drawCurveLane(pitchLane_, true);
    drawCurveLane(dynamicsLane_, false);

    // Persistent overview scrollbar: the thumb communicates both zoom and
    // location in the song, and can be clicked or dragged to navigate.
    fillRect(vg, timelineScrollBar_, nvgRGB(12, 17, 24));
    const float overviewStart = -static_cast<float>(kTicksPerQuarter);
    const float overviewEnd = std::max(overviewStart + 1.f,
        static_cast<float>(module_->score.endTick() + kTicksPerQuarter));
    const float overviewTicks = overviewEnd - overviewStart;
    const float visibleTicks = piano_.size.x / std::max(0.02f, module_->editorZoomX);
    const float trackX = timelineScrollBar_.pos.x + 4.f;
    const float trackWidth = std::max(1.f, timelineScrollBar_.size.x - 8.f);
    const float thumbWidth = std::clamp(trackWidth * visibleTicks / overviewTicks, 32.f, trackWidth);
    const float maximumScroll = std::max(overviewStart, overviewEnd - visibleTicks);
    const float scrollFraction = maximumScroll <= overviewStart
        ? 0.f : std::clamp((module_->editorScrollX - overviewStart) / (maximumScroll - overviewStart), 0.f, 1.f);
    const rack::math::Rect scrollThumb = {{trackX + scrollFraction * (trackWidth - thumbWidth),
                                           timelineScrollBar_.pos.y + 3.f},
                                          {thumbWidth, timelineScrollBar_.size.y - 6.f}};
    fillRect(vg, {{trackX, timelineScrollBar_.pos.y + timelineScrollBar_.size.y * 0.5f - 1.f},
                  {trackWidth, 2.f}}, nvgRGB(55, 66, 84), 1.f);
    fillRect(vg, scrollThumb, timelineScrollDragging_ ? kPink : nvgRGB(103, 122, 151), 3.f);
    strokeRect(vg, scrollThumb, timelineScrollDragging_ ? nvgRGB(255, 191, 215) : kTextMuted, 0.8f, 3.f);

    strokeRect(vg, ruler_, nvgRGB(48, 58, 74));
    strokeRect(vg, sectionLane_, nvgRGB(48, 58, 74));
    strokeRect(vg, piano_, nvgRGB(48, 58, 74));
    strokeRect(vg, phonemeLane_, nvgRGB(48, 58, 74));
    strokeRect(vg, pitchLane_, nvgRGB(48, 58, 74));
    strokeRect(vg, dynamicsLane_, nvgRGB(48, 58, 74));
    strokeRect(vg, timelineScrollBar_, nvgRGB(48, 58, 74));

    const float helpY = window_.pos.y + window_.size.y - 29.f;
    const float navigationY = window_.pos.y + window_.size.y - 12.f;
    std::string help;
    if (inlineLyricField_) help = "TYPE LYRIC  |  romaji to kana  |  Return commits  |  Tab advances  |  Shift+Tab returns  |  Esc cancels";
    else if (insertingLyric_) help = "INSERT LYRIC: click a gap to add a note, or split a note at the snapped position | Esc cancels";
    else if (mode_ == EditMode::Notes && noteTool_ == NoteTool::Select) help = "SELECT: click, drag, or resize notes | drag empty space for box selection | Shift adds | double-click a lyric to type";
    else if (mode_ == EditMode::Notes && noteTool_ == NoteTool::Draw) help = "PENCIL: drag empty space to draw | drag a note to move | drag either edge to resize";
    else if (mode_ == EditMode::Notes && noteTool_ == NoteTool::Erase) help = "ERASE: click a note to remove it | Undo restores it";
    else if (mode_ == EditMode::Notes) help = "SLICE: click inside a note to split it | the right half continues the vowel with +";
    else if (mode_ == EditMode::Pitch) help = "PITCH CURVE: click to add or select | drag a point in time or cents | Delete removes the selected point";
    else help = "DYNAMICS: click to add or select | drag a point in time or dB | Delete removes the selected point";
    text(vg, window_.pos.x + 14.f, helpY, 10.f, kTextMuted, help);
    text(vg, window_.pos.x + 14.f, navigationY, 9.f, kTextMuted,
         "NAVIGATE  |  Wheel: pitches  |  Shift+wheel: timeline  |  Cmd+wheel: zoom  |  Space: play/pause  |  Return: lyric  |  Esc: close");

    if (hoveredAction_ >= 0) {
        const auto found = std::find_if(actionHits_.begin(), actionHits_.end(),
                                        [&](const ActionHit& hit) { return hit.action == hoveredAction_; });
        const std::string description = actionHelp(hoveredAction_);
        if (found != actionHits_.end() && !description.empty()) {
            nvgFontFaceId(vg, APP->window->uiFont->handle);
            nvgFontSize(vg, 10.f * kEditorTextScale);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgTextLineHeight(vg, 1.15f);
            float naturalBounds[4]{};
            nvgTextBounds(vg, 0.f, 0.f, description.c_str(), nullptr, naturalBounds);
            const float naturalWidth = naturalBounds[2] - naturalBounds[0] + 22.f;
            const float width = std::clamp(naturalWidth, 220.f, 450.f);
            float wrappedBounds[4]{};
            nvgTextBoxBounds(vg, 0.f, 0.f, width - 22.f, description.c_str(), nullptr, wrappedBounds);
            const float height = std::max(31.f, wrappedBounds[3] - wrappedBounds[1] + 16.f);
            const float x = std::clamp(found->rect.pos.x, window_.pos.x + 8.f,
                                       window_.pos.x + window_.size.x - width - 8.f);
            float y = found->rect.pos.y + found->rect.size.y + 7.f;
            if (y + height > window_.pos.y + window_.size.y - 30.f)
                y = found->rect.pos.y - height - 7.f;
            const rack::math::Rect tooltip = {{x, y}, {width, height}};
            fillRect(vg, tooltip, nvgRGBA(7, 10, 15, 246), 4.f);
            strokeRect(vg, tooltip, nvgRGB(100, 117, 143), 1.f, 4.f);
            textBox(vg, tooltip.pos.x + 11.f, tooltip.pos.y + 8.f, tooltip.size.x - 22.f,
                    10.f, kText, description);
        }
    }

    Widget::draw(args);
}

void VocalEditor::setMouseCursor(GLFWcursor* cursor) {
    if (currentCursor_ == cursor || !APP || !APP->window || !APP->window->win) return;
    currentCursor_ = cursor;
    glfwSetCursor(APP->window->win, cursor);
}

void VocalEditor::onHover(const HoverEvent& e) {
    if (visualTooltipForced_) {
        OpaqueWidget::onHover(e);
        return;
    }
    hoveredAction_ = -1;
    for (const auto& hit : actionHits_) {
        if (hit.rect.contains(e.pos)) {
            hoveredAction_ = hit.action;
            break;
        }
    }
    GLFWcursor* cursor = nullptr;
    if (hoveredAction_ >= 0 || keyboard_.contains(e.pos)) {
        cursor = handCursor_;
    } else if (inspector_.contains(e.pos)) {
        const auto inspectorHit = std::find_if(inspectorControls_.begin(), inspectorControls_.end(),
            [&](const InspectorControl& control) {
                return control.value.contains(e.pos) || (control.hasSlider && control.slider.contains(e.pos));
            });
        if (inspectorHit != inspectorControls_.end()) {
            cursor = inspectorHit->hasSlider && inspectorHit->slider.contains(e.pos)
                ? handCursor_ : textCursor_;
            if (inspectorHit->field == InspectorField::VibratoEnabled) cursor = handCursor_;
        }
    } else if (timelineScrollBar_.contains(e.pos)) {
        cursor = horizontalResizeCursor_;
    } else if (piano_.contains(e.pos)) {
        const size_t noteIndex = noteAt(e.pos);
        if (noteIndex < module_->score.notes.size()) {
            const auto rect = noteRect(module_->score.notes[noteIndex]);
            const float edge = std::min(8.f, rect.size.x * 0.35f);
            if (mode_ == EditMode::Notes &&
                (noteTool_ == NoteTool::Select || noteTool_ == NoteTool::Draw) &&
                (e.pos.x <= rect.pos.x + edge || e.pos.x >= rect.pos.x + rect.size.x - edge))
                cursor = horizontalResizeCursor_;
            else if (rect.size.x > 72.f && e.pos.x >= rect.pos.x + rect.size.x - 44.f &&
                     e.pos.x < rect.pos.x + rect.size.x - 8.f)
                cursor = textCursor_;
            else if (noteTool_ == NoteTool::Slice)
                cursor = crosshairCursor_;
            else
                cursor = handCursor_;
        } else {
            cursor = mode_ == EditMode::Notes &&
                     (noteTool_ == NoteTool::Draw || noteTool_ == NoteTool::Slice) ? crosshairCursor_ : nullptr;
        }
    } else if (pitchLane_.contains(e.pos) || dynamicsLane_.contains(e.pos)) {
        cursor = crosshairCursor_;
    } else if (phonemeLane_.contains(e.pos) && !selection_.empty()) {
        cursor = horizontalResizeCursor_;
    }
    setMouseCursor(cursor);
    OpaqueWidget::onHover(e);
}

void VocalEditor::onLeave(const LeaveEvent& e) {
    if (!visualTooltipForced_) hoveredAction_ = -1;
    setMouseCursor(nullptr);
    OpaqueWidget::onLeave(e);
}

bool VocalEditor::addCurvePoint(rack::math::Vec pos) {
    if (selection_.empty()) return false;
    const size_t index = *selection_.begin();
    if (index >= module_->score.notes.size()) return false;
    auto& note = module_->score.notes[index];
    const int64_t offset = snapTick(xToTick(pos.x)) - note.startTick;
    if (offset < 0 || offset > note.durationTick) return false;

    const std::string before = scoreToJson(module_->score);
    if (mode_ == EditMode::Pitch && pitchLane_.contains(pos)) {
        const float normalized = std::clamp((pos.y - pitchLane_.pos.y) / pitchLane_.size.y, 0.f, 1.f);
        const float value = (0.5f - normalized) * 400.f;
        auto existing = std::find_if(note.pitchCents.points.begin(), note.pitchCents.points.end(),
                                     [&](const CurvePoint& point) { return point.tickOffset == offset; });
        if (existing != note.pitchCents.points.end()) existing->value = value;
        else note.pitchCents.points.push_back({offset, value});
        note.pitchCents.normalize();
        curveNoteIndex_ = index;
        curvePointPitch_ = true;
        curvePointIndex_ = static_cast<size_t>(std::find_if(note.pitchCents.points.begin(), note.pitchCents.points.end(),
            [&](const CurvePoint& point) { return point.tickOffset == offset; }) - note.pitchCents.points.begin());
        module_->commitScoreEdit(before, "Draw pitch curve");
        return true;
    }
    if (mode_ == EditMode::Dynamics && dynamicsLane_.contains(pos)) {
        const float normalized = std::clamp((pos.y - dynamicsLane_.pos.y) / dynamicsLane_.size.y, 0.f, 1.f);
        const float value = 12.f - normalized * 36.f;
        auto existing = std::find_if(note.dynamicsDb.points.begin(), note.dynamicsDb.points.end(),
                                     [&](const CurvePoint& point) { return point.tickOffset == offset; });
        if (existing != note.dynamicsDb.points.end()) existing->value = value;
        else note.dynamicsDb.points.push_back({offset, value});
        note.dynamicsDb.normalize();
        curveNoteIndex_ = index;
        curvePointPitch_ = false;
        curvePointIndex_ = static_cast<size_t>(std::find_if(note.dynamicsDb.points.begin(), note.dynamicsDb.points.end(),
            [&](const CurvePoint& point) { return point.tickOffset == offset; }) - note.dynamicsDb.points.begin());
        module_->commitScoreEdit(before, "Draw dynamics curve");
        return true;
    }
    return false;
}

void VocalEditor::clearCurvePointSelection() {
    curveDragging_ = false;
    curveNoteIndex_ = std::numeric_limits<size_t>::max();
    curvePointIndex_ = std::numeric_limits<size_t>::max();
}

bool VocalEditor::startCurvePointDrag(rack::math::Vec pos) {
    if (selection_.empty() || (mode_ != EditMode::Pitch && mode_ != EditMode::Dynamics)) return false;
    const size_t noteIndex = *selection_.begin();
    if (noteIndex >= module_->score.notes.size()) return false;
    const bool pitch = mode_ == EditMode::Pitch;
    const auto& lane = pitch ? pitchLane_ : dynamicsLane_;
    if (!lane.contains(pos)) return false;
    const auto& note = module_->score.notes[noteIndex];
    const auto& points = pitch ? note.pitchCents.points : note.dynamicsDb.points;
    const float noteX = piano_.pos.x + (note.startTick - module_->editorScrollX) * module_->editorZoomX;
    for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex) {
        const auto& point = points[pointIndex];
        const float x = noteX + point.tickOffset * module_->editorZoomX;
        const float y = pitch ? lane.pos.y + lane.size.y * (0.5f - std::clamp(point.value, -200.f, 200.f) / 400.f)
                              : lane.pos.y + lane.size.y * (12.f - std::clamp(point.value, -24.f, 12.f)) / 36.f;
        const float dx = pos.x - x;
        const float dy = pos.y - y;
        if (dx * dx + dy * dy <= 64.f) {
            curveNoteIndex_ = noteIndex;
            curvePointIndex_ = pointIndex;
            curvePointPitch_ = pitch;
            curvePointStart_ = point;
            curveDragBefore_ = scoreToJson(module_->score);
            curveDragPixels_ = {};
            curveDragging_ = true;
            return true;
        }
    }
    return false;
}

bool VocalEditor::startPhonemeTimingDrag(rack::math::Vec pos) {
    if (!phonemeLane_.contains(pos) || selection_.empty()) return false;
    const auto diagnostics = module_->renderSlot->copyDiagnostics();
    const double ticksPerMs = module_->score.nominalBpm * kTicksPerQuarter / 60000.0;
    for (const auto noteIndex : selection_) {
        if (noteIndex >= module_->score.notes.size()) continue;
        const auto& note = module_->score.notes[noteIndex];
        const auto phone = std::find_if(diagnostics.phonemes.begin(), diagnostics.phonemes.end(),
            [&](const PhonemeEvent& candidate) { return candidate.sourceNoteId == note.id && candidate.oto; });
        if (phone == diagnostics.phonemes.end()) continue;
        const float positionX = piano_.pos.x +
            (note.startTick + note.phonemeTiming.positionOffsetTick.value_or(0) - module_->editorScrollX) *
                module_->editorZoomX;
        const float actualPreutter = std::clamp(static_cast<float>(phone->oto->preutterMs) +
            note.phonemeTiming.preutteranceDeltaMs.value_or(0.f), 0.f, 500.f);
        const float actualOverlap = std::clamp(static_cast<float>(phone->oto->overlapMs) +
            note.phonemeTiming.overlapDeltaMs.value_or(0.f), -500.f, actualPreutter);
        const float startX = positionX - static_cast<float>(actualPreutter * ticksPerMs * module_->editorZoomX);
        const float overlapX = startX + static_cast<float>(actualOverlap * ticksPerMs * module_->editorZoomX);
        const float startY = phonemeLane_.pos.y + phonemeLane_.size.y - 13.f;
        const float overlapY = phonemeLane_.pos.y + 13.f;
        auto close = [&](float x, float y) {
            const float dx = pos.x - x, dy = pos.y - y;
            return dx * dx + dy * dy <= 121.f;
        };
        if (close(startX, startY)) {
            timingHandle_ = TimingHandle::Preutterance;
            timingStartActualMs_ = actualPreutter;
            timingOtoMs_ = static_cast<float>(phone->oto->preutterMs);
        } else if (close(overlapX, overlapY)) {
            timingHandle_ = TimingHandle::Overlap;
            timingStartActualMs_ = actualOverlap;
            timingOtoMs_ = static_cast<float>(phone->oto->overlapMs);
        } else {
            continue;
        }
        timingNoteIndex_ = noteIndex;
        timingDragBefore_ = scoreToJson(module_->score);
        timingDragPixels_ = {};
        timingDragging_ = true;
        clearCurvePointSelection();
        return true;
    }
    return false;
}

void VocalEditor::setTimelineScrollFromX(float x) {
    module_->editorFollowPlayhead = false;
    const float overviewStart = -static_cast<float>(kTicksPerQuarter);
    const float overviewEnd = std::max(overviewStart + 1.f,
        static_cast<float>(module_->score.endTick() + kTicksPerQuarter));
    const float visibleTicks = piano_.size.x / std::max(0.02f, module_->editorZoomX);
    const float maximumScroll = std::max(overviewStart, overviewEnd - visibleTicks);
    const float trackWidth = std::max(1.f, timelineScrollBar_.size.x - 8.f);
    const float thumbWidth = std::clamp(trackWidth * visibleTicks / (overviewEnd - overviewStart), 32.f, trackWidth);
    const float travel = trackWidth - thumbWidth;
    const float fraction = travel <= 0.f ? 0.f
        : std::clamp((x - (timelineScrollBar_.pos.x + 4.f) - thumbWidth * 0.5f) / travel, 0.f, 1.f);
    module_->editorScrollX = overviewStart + fraction * (maximumScroll - overviewStart);
}

bool VocalEditor::handleInspectorPress(rack::math::Vec pos) {
    if (!inspector_.contains(pos)) return false;
    const auto restore = std::find_if(actionHits_.begin(), actionHits_.end(),
        [](const ActionHit& hit) { return hit.action == 35; });
    if (restore != actionHits_.end() && restore->rect.contains(pos)) {
        toolbarAction(35);
        return true;
    }
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return true;
    for (const auto& control : inspectorControls_) {
        if (!control.row.contains(pos) && !control.value.contains(pos) &&
            !(control.hasSlider && control.slider.contains(pos))) continue;
        if (control.field == InspectorField::VibratoEnabled) {
            if (inspectorValueField_) finishInspectorEdit(true);
            const auto before = scoreToJson(module_->score);
            auto& vibrato = module_->score.notes[*selection_.begin()].vibrato;
            vibrato.depthCents = vibrato.depthCents > 0.f ? 0.f : 25.f;
            inspectorError_.clear();
            module_->commitScoreEdit(before, vibrato.depthCents > 0.f ? "Enable vibrato" : "Disable vibrato");
            return true;
        }
        if (!control.hasSlider || control.value.contains(pos)) {
            pendingInspectorEditField_ = control.field;
            pendingInspectorEditFrames_ = 1;
            return true;
        }
        if (control.slider.contains(pos)) {
            const bool vibratoOff = module_->score.notes[*selection_.begin()].vibrato.depthCents <= 0.f;
            if (vibratoOff && (control.field == InspectorField::VibratoStart ||
                               control.field == InspectorField::VibratoRate)) return true;
            if (inspectorValueField_) finishInspectorEdit(true);
            inspectorSliderDragging_ = true;
            inspectorSliderField_ = control.field;
            inspectorSliderBefore_ = scoreToJson(module_->score);
            inspectorSliderX_ = pos.x;
            const auto range = inspectorRange(control.field);
            const float normalized = std::clamp((inspectorSliderX_ - control.slider.pos.x) /
                std::max(1.f, control.slider.size.x), 0.f, 1.f);
            setInspectorNumericValue(control.field,
                range.first + normalized * (range.second - range.first));
            inspectorError_.clear();
            return true;
        }
        return true;
    }
    return true;
}

void VocalEditor::handleInspectorDrag(rack::math::Vec delta) {
    if (!inspectorSliderDragging_) return;
    inspectorSliderX_ += delta.x;
    module_->score = scoreFromJson(inspectorSliderBefore_);
    const auto control = std::find_if(inspectorControls_.begin(), inspectorControls_.end(),
        [&](const InspectorControl& candidate) { return candidate.field == inspectorSliderField_; });
    if (control == inspectorControls_.end()) return;
    const auto range = inspectorRange(inspectorSliderField_);
    const float normalized = std::clamp((inspectorSliderX_ - control->slider.pos.x) /
        std::max(1.f, control->slider.size.x), 0.f, 1.f);
    setInspectorNumericValue(inspectorSliderField_, range.first + normalized * (range.second - range.first));
}

void VocalEditor::handleInspectorDragEnd() {
    if (!inspectorSliderDragging_) return;
    inspectorSliderDragging_ = false;
    module_->score.normalize();
    module_->commitScoreEdit(inspectorSliderBefore_, "Adjust note inspector slider");
    inspectorSliderField_ = InspectorField::None;
    inspectorSliderBefore_.clear();
}

void VocalEditor::onButton(const ButtonEvent& e) {
    if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT && auditionHolding_) {
        module_->endAudition();
        auditionHolding_ = false;
        auditionKeyboardDrag_ = false;
    }
    if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT && pendingMenuAction_ >= 0) {
        const int action = pendingMenuAction_;
        pendingMenuAction_ = -1;
        toolbarAction(action);
        e.consume(this);
        return;
    }
    if (e.action != GLFW_PRESS) {
        OpaqueWidget::onButton(e);
        return;
    }
    if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
        openContextMenu(e.pos);
        e.consume(this);
        return;
    }
    if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
        OpaqueWidget::onButton(e);
        return;
    }
    for (const auto& hit : actionHits_) {
        if (hit.rect.contains(e.pos)) {
            hoveredAction_ = hit.action;
            if (hit.action == 6 || hit.action == 32 || hit.action == 33 || hit.action == 37) {
                pendingMenuAction_ = hit.action;
                e.consume(this);
                return;
            }
            toolbarAction(hit.action);
            e.consume(this);
            return;
        }
    }
    if (handleInspectorPress(e.pos)) {
        e.consume(this);
        return;
    }
    if (timelineScrollBar_.contains(e.pos)) {
        timelineScrollDragging_ = true;
        timelineScrollDragX_ = e.pos.x;
        setTimelineScrollFromX(timelineScrollDragX_);
        e.consume(this);
        return;
    }
    if (keyboard_.contains(e.pos)) {
        auditionHolding_ = true;
        auditionKeyboardDrag_ = true;
        auditionPointerY_ = e.pos.y;
        lastDragAuditionMidi_ = yToMidi(e.pos.y);
        module_->beginAuditionMidiNote(lastDragAuditionMidi_);
        e.consume(this);
        return;
    }
    if (piano_.contains(e.pos)) {
        mode_ = EditMode::Notes;
        clearCurvePointSelection();
    }
    if (startPhonemeTimingDrag(e.pos)) {
        e.consume(this);
        return;
    }
    if (startCurvePointDrag(e.pos) || addCurvePoint(e.pos)) {
        e.consume(this);
        return;
    }
    if (phonemeLane_.contains(e.pos)) {
        const size_t phonemeHit = noteAtTick(xToTick(e.pos.x));
        if (phonemeHit < module_->score.notes.size()) {
            if (!(e.mods & GLFW_MOD_SHIFT)) selection_.clear();
            selection_.insert(phonemeHit);
        }
        e.consume(this);
        return;
    }
    if (!piano_.contains(e.pos)) {
        e.consume(this);
        return;
    }

    if (insertingLyric_) {
        try { insertLyricAt(e.pos); }
        catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
        e.consume(this);
        return;
    }

    const size_t hit = noteAt(e.pos);
    if (noteTool_ == NoteTool::Erase) {
        if (hit < module_->score.notes.size()) {
            selection_.clear();
            selection_.insert(hit);
            deleteSelectedNotes();
        }
        e.consume(this);
        return;
    }
    if (noteTool_ == NoteTool::Slice) {
        if (hit < module_->score.notes.size()) sliceNoteAt(hit, xToTick(e.pos.x));
        e.consume(this);
        return;
    }
    if (noteTool_ == NoteTool::Draw) {
        if (hit >= module_->score.notes.size()) {
            beginDrawNote(e.pos);
            e.consume(this);
            return;
        }
    }
    if (hit < module_->score.notes.size()) {
        if ((e.mods & GLFW_MOD_SHIFT) && selection_.count(hit)) {
            selection_.erase(hit);
            e.consume(this);
            return;
        }
        // Clicking one member of an existing marquee/shift selection starts
        // a group drag. Collapsing the selection here made only the grabbed
        // note move, and its former neighbours then looked like collisions.
        if (e.mods & GLFW_MOD_SHIFT) {
            selection_.insert(hit);
        } else if (!selection_.count(hit)) {
            selection_.clear();
            selection_.insert(hit);
        }
        auditionHolding_ = true;
        auditionKeyboardDrag_ = false;
        module_->beginAuditionMidiNote(module_->score.notes[hit].midiNote);
        lastDragAuditionMidi_ = module_->score.notes[hit].midiNote;
        const auto clickedRect = noteRect(module_->score.notes[hit]);
        const float rightEdge = clickedRect.pos.x + clickedRect.size.x;
        if (noteTool_ == NoteTool::Select && !(e.mods & GLFW_MOD_SHIFT) && clickedRect.size.x > 72.f &&
            e.pos.x >= rightEdge - 44.f && e.pos.x < rightEdge - 8.f) {
            dragging_ = false;
            beginInlineLyric(hit);
            e.consume(this);
            return;
        }
        const double now = rack::system::getTime();
        const bool doubleClick = hit == lastClickedNoteIndex_ && now - lastNoteClickTime_ <= 0.40;
        lastClickedNoteIndex_ = hit;
        lastNoteClickTime_ = now;
        if (noteTool_ == NoteTool::Select && doubleClick && !(e.mods & GLFW_MOD_SHIFT)) {
            dragging_ = false;
            beginInlineLyric(hit);
            e.consume(this);
            return;
        }
        if (mode_ == EditMode::Notes) {
            clearCurvePointSelection();
            dragBefore_ = scoreToJson(module_->score);
            dragging_ = true;
            dragPixels_ = {};
            dragPrimaryNoteId_ = module_->score.notes[hit].id;
            dragSelectionIds_.clear();
            dragSelectionIds_.reserve(selection_.size());
            for (const size_t selected : selection_)
                if (selected < module_->score.notes.size())
                    dragSelectionIds_.push_back(module_->score.notes[selected].id);
            const auto rect = noteRect(module_->score.notes[hit]);
            resizingStart_ = e.pos.x < rect.pos.x + std::min(8.f, rect.size.x * 0.35f);
            resizing_ = resizingStart_ || e.pos.x > rect.pos.x + rect.size.x - std::min(8.f, rect.size.x * 0.35f);
        }
    } else if (mode_ == EditMode::Notes && noteTool_ == NoteTool::Select) {
        marqueeDragging_ = true;
        marqueeStart_ = e.pos;
        marqueeCurrent_ = e.pos;
        marqueeBaseSelection_ = (e.mods & GLFW_MOD_SHIFT) ? selection_ : std::set<size_t>{};
        selection_ = marqueeBaseSelection_;
    }
    e.consume(this);
}

void VocalEditor::onDoubleClick(const DoubleClickEvent& e) {
    if (noteTool_ != NoteTool::Select) return;
    if (lastClickedNoteIndex_ >= module_->score.notes.size()) return;
    dragging_ = false;
    resizing_ = false;
    resizingStart_ = false;
    selection_.clear();
    selection_.insert(lastClickedNoteIndex_);
    beginInlineLyric(lastClickedNoteIndex_);
    e.consume(this);
}

void VocalEditor::onDragMove(const DragMoveEvent& e) {
    if (auditionHolding_ && auditionKeyboardDrag_) {
        auditionPointerY_ = std::clamp(auditionPointerY_ + e.mouseDelta.y,
                                       keyboard_.pos.y, keyboard_.pos.y + keyboard_.size.y);
        const int midi = yToMidi(auditionPointerY_);
        if (midi != lastDragAuditionMidi_) {
            lastDragAuditionMidi_ = midi;
            module_->beginAuditionMidiNote(midi);
        }
        return;
    }
    if (drawingNote_) {
        drawCurrent_.x = std::clamp(drawCurrent_.x + e.mouseDelta.x,
                                    piano_.pos.x, piano_.pos.x + piano_.size.x);
        drawCurrent_.y = std::clamp(drawCurrent_.y + e.mouseDelta.y,
                                    piano_.pos.y, piano_.pos.y + piano_.size.y);
        updateDrawNote();
        const int midi = yToMidi(drawCurrent_.y);
        if (midi != lastDragAuditionMidi_) {
            lastDragAuditionMidi_ = midi;
            module_->beginAuditionMidiNote(midi);
        }
        return;
    }
    if (marqueeDragging_) {
        marqueeCurrent_.x = std::clamp(marqueeCurrent_.x + e.mouseDelta.x,
                                       piano_.pos.x, piano_.pos.x + piano_.size.x);
        marqueeCurrent_.y = std::clamp(marqueeCurrent_.y + e.mouseDelta.y,
                                       piano_.pos.y, piano_.pos.y + piano_.size.y);
        const float left = std::min(marqueeStart_.x, marqueeCurrent_.x);
        const float right = std::max(marqueeStart_.x, marqueeCurrent_.x);
        const float top = std::min(marqueeStart_.y, marqueeCurrent_.y);
        const float bottom = std::max(marqueeStart_.y, marqueeCurrent_.y);
        selection_ = marqueeBaseSelection_;
        for (size_t index = 0; index < module_->score.notes.size(); ++index) {
            const auto rect = noteRect(module_->score.notes[index]);
            const bool intersects = rect.pos.x + rect.size.x >= left && rect.pos.x <= right &&
                                    rect.pos.y + rect.size.y >= top && rect.pos.y <= bottom;
            if (intersects) selection_.insert(index);
        }
        return;
    }
    if (inspectorSliderDragging_) {
        handleInspectorDrag(e.mouseDelta);
        return;
    }
    if (timelineScrollDragging_) {
        timelineScrollDragX_ += e.mouseDelta.x;
        setTimelineScrollFromX(timelineScrollDragX_);
        return;
    }
    if (timingDragging_) {
        timingDragPixels_ = timingDragPixels_.plus(e.mouseDelta);
        module_->score = scoreFromJson(timingDragBefore_);
        if (timingNoteIndex_ >= module_->score.notes.size()) return;
        const double ticksPerMs = module_->score.nominalBpm * kTicksPerQuarter / 60000.0;
        const float deltaMs = static_cast<float>(timingDragPixels_.x /
            std::max(0.0001, ticksPerMs * module_->editorZoomX));
        auto& timing = module_->score.notes[timingNoteIndex_].phonemeTiming;
        const float actual = timingHandle_ == TimingHandle::Preutterance
            ? std::clamp(timingStartActualMs_ - deltaMs, 0.f, 500.f)
            : std::clamp(timingStartActualMs_ + deltaMs, -500.f, 500.f);
        const float adjustment = actual - timingOtoMs_;
        const auto overrideValue = std::abs(adjustment) < 0.05f
            ? std::optional<float>{} : std::optional<float>{adjustment};
        if (timingHandle_ == TimingHandle::Preutterance) timing.preutteranceDeltaMs = overrideValue;
        else timing.overlapDeltaMs = overrideValue;
        return;
    }
    if (curveDragging_) {
        curveDragPixels_ = curveDragPixels_.plus(e.mouseDelta);
        module_->score = scoreFromJson(curveDragBefore_);
        if (curveNoteIndex_ >= module_->score.notes.size()) return;
        auto& note = module_->score.notes[curveNoteIndex_];
        auto& points = curvePointPitch_ ? note.pitchCents.points : note.dynamicsDb.points;
        if (curvePointIndex_ >= points.size()) return;
        const int64_t rawDelta = static_cast<int64_t>(std::llround(curveDragPixels_.x / module_->editorZoomX));
        const int64_t grid = std::max<int64_t>(1, module_->editorSnapTick);
        const int64_t deltaTick = module_->editorSnapEnabled
            ? static_cast<int64_t>(std::llround(static_cast<double>(rawDelta) / grid)) * grid
            : rawDelta;
        auto& point = points[curvePointIndex_];
        point.tickOffset = std::clamp(curvePointStart_.tickOffset + deltaTick, int64_t{0}, note.durationTick);
        const float scale = curvePointPitch_ ? 400.f / pitchLane_.size.y : 36.f / dynamicsLane_.size.y;
        point.value = curvePointStart_.value - curveDragPixels_.y * scale;
        point.value = curvePointPitch_ ? std::clamp(point.value, -200.f, 200.f)
                                      : std::clamp(point.value, -24.f, 12.f);
        return;
    }
    if (!dragging_ || selection_.empty()) return;
    dragPixels_ = dragPixels_.plus(e.mouseDelta);
    const int64_t rawDelta = static_cast<int64_t>(std::llround(dragPixels_.x / module_->editorZoomX));
    const int64_t grid = std::max<int64_t>(1, module_->editorSnapTick);
    const int64_t minimumDuration = module_->editorSnapEnabled ? grid : 1;
    int64_t deltaTick = module_->editorSnapEnabled
        ? static_cast<int64_t>(std::llround(static_cast<double>(rawDelta) / grid)) * grid
        : rawDelta;
    int deltaPitch = static_cast<int>(std::lround(-dragPixels_.y / module_->editorZoomY));
    module_->score = scoreFromJson(dragBefore_);

    // A body drag is a rigid translation: clamp the delta once for the
    // entire selection rather than clamping each note independently.
    if (!resizing_ && !resizingStart_) {
        int64_t firstStart = std::numeric_limits<int64_t>::max();
        int lowestPitch = 127;
        int highestPitch = 0;
        for (const size_t index : selection_) {
            if (index >= module_->score.notes.size()) continue;
            const auto& note = module_->score.notes[index];
            firstStart = std::min(firstStart, note.startTick);
            lowestPitch = std::min(lowestPitch, note.midiNote);
            highestPitch = std::max(highestPitch, note.midiNote);
        }
        if (firstStart != std::numeric_limits<int64_t>::max())
            deltaTick = std::max(deltaTick, -firstStart);
        deltaPitch = std::clamp(deltaPitch, -lowestPitch, 127 - highestPitch);
    }
    for (const size_t index : selection_) {
        if (index >= module_->score.notes.size()) continue;
        auto& note = module_->score.notes[index];
        if (resizingStart_) {
            if (note.id != dragPrimaryNoteId_) continue;
            const int64_t originalEnd = note.endTick();
            note.startTick = std::clamp(snapTick(note.startTick + deltaTick), int64_t{0}, originalEnd - minimumDuration);
            note.durationTick = originalEnd - note.startTick;
        } else if (resizing_) {
            if (note.id != dragPrimaryNoteId_) continue;
            note.durationTick = std::max<int64_t>(minimumDuration,
                module_->editorSnapEnabled ? snapTick(note.durationTick + deltaTick)
                                           : note.durationTick + deltaTick);
        } else {
            note.startTick += deltaTick;
            note.midiNote = std::clamp(note.midiNote + deltaPitch, 0, 127);
        }
    }
    if (!resizing_ && !resizingStart_ && selection_.size() == 1) {
        const size_t index = *selection_.begin();
        if (index < module_->score.notes.size() && module_->score.notes[index].midiNote != lastDragAuditionMidi_) {
            lastDragAuditionMidi_ = module_->score.notes[index].midiNote;
            module_->beginAuditionMidiNote(lastDragAuditionMidi_);
        }
    }
}

void VocalEditor::onDragEnd(const DragEndEvent&) {
    if (auditionHolding_) {
        module_->endAudition();
        auditionHolding_ = false;
        auditionKeyboardDrag_ = false;
    }
    if (drawingNote_) {
        finishDrawNote();
        return;
    }
    if (marqueeDragging_) {
        marqueeDragging_ = false;
        marqueeBaseSelection_.clear();
        return;
    }
    if (inspectorSliderDragging_) {
        handleInspectorDragEnd();
        return;
    }
    if (timelineScrollDragging_) {
        timelineScrollDragging_ = false;
        return;
    }
    if (timingDragging_) {
        timingDragging_ = false;
        module_->score.normalize();
        module_->commitScoreEdit(timingDragBefore_,
            timingHandle_ == TimingHandle::Preutterance ? "Move phoneme start" : "Move phoneme overlap");
        timingHandle_ = TimingHandle::None;
        timingNoteIndex_ = std::numeric_limits<size_t>::max();
        return;
    }
    if (curveDragging_) {
        curveDragging_ = false;
        if (curveNoteIndex_ < module_->score.notes.size()) {
            auto& curve = curvePointPitch_ ? module_->score.notes[curveNoteIndex_].pitchCents
                                          : module_->score.notes[curveNoteIndex_].dynamicsDb;
            const int64_t selectedTick = curvePointIndex_ < curve.points.size()
                                             ? curve.points[curvePointIndex_].tickOffset : 0;
            curve.normalize();
            const auto selected = std::find_if(curve.points.begin(), curve.points.end(),
                [&](const CurvePoint& point) { return point.tickOffset == selectedTick; });
            curvePointIndex_ = selected == curve.points.end() ? std::numeric_limits<size_t>::max()
                                                               : static_cast<size_t>(selected - curve.points.begin());
        }
        module_->commitScoreEdit(curveDragBefore_, curvePointPitch_ ? "Move pitch point" : "Move dynamics point");
        return;
    }
    if (!dragging_) return;
    dragging_ = false;
    module_->score.normalize();
    if (!module_->score.validate().empty()) {
        module_->score = scoreFromJson(dragBefore_);
    } else {
        module_->commitScoreEdit(dragBefore_, (resizing_ || resizingStart_)
            ? "Resize note"
            : (dragSelectionIds_.size() > 1 ? "Move selected notes" : "Move note"));
    }
    // normalize() may reorder notes after a move across the timeline. Restore
    // the visible selection by stable note id instead of stale vector index.
    selection_.clear();
    for (size_t index = 0; index < module_->score.notes.size(); ++index)
        if (std::find(dragSelectionIds_.begin(), dragSelectionIds_.end(),
                      module_->score.notes[index].id) != dragSelectionIds_.end())
            selection_.insert(index);
    dragSelectionIds_.clear();
    dragPrimaryNoteId_.clear();
    resizing_ = false;
    resizingStart_ = false;
}

void VocalEditor::onHoverScroll(const HoverScrollEvent& e) {
    module_->editorFollowPlayhead = false;
    const int mods = APP->window->getMods();
    // macOS trackpads can report a large burst for one gesture.  Treat the
    // delta as a bounded wheel gesture and pan by screen pixels, not a fixed
    // number of musical ticks.  This keeps navigation calm at Fit Song and
    // precise when the timeline is zoomed in.
    const float wheel = std::clamp(e.scrollDelta.y, -2.f, 2.f);
    if ((mods & RACK_MOD_CTRL) && (mods & GLFW_MOD_SHIFT))
        module_->editorZoomY = std::clamp(module_->editorZoomY * std::pow(1.08f, wheel), 4.f, 40.f);
    else if (mods & RACK_MOD_CTRL)
        module_->editorZoomX = std::clamp(module_->editorZoomX * std::pow(1.08f, wheel), 0.02f, 2.f);
    else if (mods & GLFW_MOD_SHIFT) {
        const float panTicks = wheel * 28.f / std::max(0.02f, module_->editorZoomX);
        const float minimumScroll = -static_cast<float>(kTicksPerQuarter);
        const float visibleTicks = piano_.size.x / std::max(0.02f, module_->editorZoomX);
        const float maximumScroll = std::max(minimumScroll,
            static_cast<float>(module_->score.endTick() + kTicksPerQuarter) - visibleTicks);
        module_->editorScrollX = std::clamp(module_->editorScrollX - panTicks, minimumScroll, maximumScroll);
    } else
        module_->editorScrollY = std::clamp(module_->editorScrollY + wheel * 18.f, -1200.f, 1200.f);
    e.consume(this);
}

void VocalEditor::onSelectKey(const SelectKeyEvent& e) {
    if (e.action != GLFW_PRESS && e.action != GLFW_REPEAT) return;
    if (e.key == GLFW_KEY_ESCAPE) {
        if (insertingLyric_) insertingLyric_ = false;
        else requestDelete();
    } else if (e.key == GLFW_KEY_DELETE || e.key == GLFW_KEY_BACKSPACE) {
        if (curveNoteIndex_ < module_->score.notes.size() && curvePointIndex_ != std::numeric_limits<size_t>::max()) {
            const auto before = scoreToJson(module_->score);
            auto& curve = curvePointPitch_ ? module_->score.notes[curveNoteIndex_].pitchCents
                                          : module_->score.notes[curveNoteIndex_].dynamicsDb;
            if (curvePointIndex_ < curve.points.size()) curve.points.erase(curve.points.begin() + curvePointIndex_);
            clearCurvePointSelection();
            module_->commitScoreEdit(before, "Delete curve point");
            e.consume(this);
            return;
        }
        deleteSelectedNotes();
    } else if (e.isKeyCommand(GLFW_KEY_A, RACK_MOD_CTRL)) {
        selection_.clear();
        for (size_t index = 0; index < module_->score.notes.size(); ++index)
            selection_.insert(index);
        clearCurvePointSelection();
    } else if (e.isKeyCommand(GLFW_KEY_C, RACK_MOD_CTRL)) {
        copySelectedNotes();
    } else if (e.isKeyCommand(GLFW_KEY_V, RACK_MOD_CTRL) && !clipboard_.empty()) {
        const int64_t first = std::min_element(clipboard_.begin(), clipboard_.end(),
            [](const Note& a, const Note& b) { return a.startTick < b.startTick; })->startTick;
        try { pasteClipboardAtTick(first + kTicksPerQuarter); }
        catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
    } else if (e.isKeyCommand(GLFW_KEY_Z, RACK_MOD_CTRL | RACK_MOD_SHIFT)) {
        module_->redo();
    } else if (e.isKeyCommand(GLFW_KEY_Z, RACK_MOD_CTRL)) {
        module_->undo();
    } else if (e.isKeyCommand(GLFW_KEY_S, RACK_MOD_CTRL | RACK_MOD_SHIFT)) {
        exportVocalUstxFile(module_);
    } else if (e.isKeyCommand(GLFW_KEY_S, RACK_MOD_CTRL)) {
        saveVocalProjectFile(module_);
    } else if (e.key == GLFW_KEY_I && (e.mods & GLFW_MOD_SHIFT) && !(e.mods & RACK_MOD_CTRL)) {
        try { importUstx(); }
        catch (const std::exception& error) { osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what()); }
    } else if (e.key == GLFW_KEY_SPACE && e.action == GLFW_PRESS) {
        toolbarAction(28);
    } else if (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER) {
        if (!selection_.empty()) beginInlineLyric(*selection_.begin());
    } else if (e.key == GLFW_KEY_V && !(e.mods & RACK_MOD_CTRL)) {
        toolbarAction(40);
    } else if (e.key == GLFW_KEY_D && !(e.mods & RACK_MOD_CTRL)) {
        toolbarAction(41);
    } else if (e.key == GLFW_KEY_E && !(e.mods & RACK_MOD_CTRL)) {
        toolbarAction(42);
    } else if (e.key == GLFW_KEY_S && !(e.mods & RACK_MOD_CTRL)) {
        toolbarAction(43);
    } else if (e.key == GLFW_KEY_1 && !(e.mods & RACK_MOD_CTRL)) {
        mode_ = EditMode::Notes;
        insertingLyric_ = false;
        clearCurvePointSelection();
    } else if (e.key == GLFW_KEY_2 && !(e.mods & RACK_MOD_CTRL)) {
        mode_ = EditMode::Pitch;
        insertingLyric_ = false;
        clearCurvePointSelection();
    } else if (e.key == GLFW_KEY_3 && !(e.mods & RACK_MOD_CTRL)) {
        mode_ = EditMode::Dynamics;
        insertingLyric_ = false;
        clearCurvePointSelection();
    } else if (e.key == GLFW_KEY_I && !(e.mods & RACK_MOD_CTRL)) {
        insertingLyric_ = !insertingLyric_;
        mode_ = EditMode::Notes;
    } else {
        return;
    }
    e.consume(this);
}

void VocalEditor::toolbarAction(int action) {
    try {
        if (action == 0) {
            module_->phonemizerName = kEnglishToJapanesePhonemizer;
            if (module_->singerId != "builtin:adachi-rei")
                module_->phonemizerName = kEnglishXSampaPhonemizer;
            module_->replaceScore(makeDefaultScore(), "New English first-sound phrase");
            module_->params[VocalModule::LOOP_PARAM].setValue(0.f);
            module_->params[VocalModule::RANGE_PARAM].setValue(0.f);
            module_->panelPlaying.store(true);
            selection_.clear();
            clearCurvePointSelection();
            fitPitchRange();
            zoomFull();
        } else if (action == 1) {
            auto empty = module_->score;
            empty.title = "Empty vocal score";
            empty.notes.clear();
            empty.sections.clear();
            module_->replaceScore(std::move(empty), "Clear score");
            selection_.clear();
        } else if (action == 2) {
            importUstx();
        } else if (action == 3) {
            selectSinger();
        } else if (action == 4) {
            const char* englishMode = module_->singerId == "builtin:adachi-rei"
                ? kEnglishToJapanesePhonemizer : kEnglishXSampaPhonemizer;
            module_->phonemizerName = module_->phonemizerName == englishMode
                ? kJapaneseAutoPhonemizer : englishMode;
            module_->requestRerender();
        } else if (action == 5) {
            const auto value = prompt("Nominal BPM and time signature (example: 120 4/4)",
                                      std::to_string(module_->score.nominalBpm) + " " +
                                          std::to_string(module_->score.beatsPerBar) + "/" +
                                          std::to_string(module_->score.beatUnit));
            if (value && !value->empty()) {
                double bpm;
                int beats, unit;
                char slash;
                std::stringstream input(*value);
                if (input >> bpm >> beats >> slash >> unit && slash == '/' && std::isfinite(bpm) &&
                    bpm >= 20.0 && bpm <= 400.0 && beats >= 1 && beats <= 32 && unit >= 1 && unit <= 32) {
                    const auto before = scoreToJson(module_->score);
                    module_->score.nominalBpm = bpm;
                    module_->score.beatsPerBar = beats;
                    module_->score.beatUnit = unit;
                    module_->commitScoreEdit(before, "Tempo/time signature");
                }
            }
        } else if (action == 6) {
            openSnapMenu();
        } else if (action == 7) {
            const auto value = prompt("Jump to tick, or prefix a bar number with b (example: b12)", "0");
            if (value && !value->empty()) {
                const int64_t tick = (*value)[0] == 'b' || (*value)[0] == 'B'
                                         ? (std::max<int64_t>(1, std::stoll(value->substr(1))) - 1) * module_->score.beatsPerBar * kTicksPerQuarter
                                         : std::stoll(*value);
                module_->editorScrollX = static_cast<float>(std::max<int64_t>(0, tick));
                module_->editorFollowPlayhead = false;
            }
        } else if (action >= 8 && action <= 10) {
            const EditMode requested = static_cast<EditMode>(action - 8);
            mode_ = requested != EditMode::Notes && mode_ == requested ? EditMode::Notes : requested;
            clearCurvePointSelection();
        } else if (action >= 40 && action <= 43) {
            noteTool_ = static_cast<NoteTool>(action - 40);
            mode_ = EditMode::Notes;
            insertingLyric_ = false;
            clearCurvePointSelection();
        } else if (action == 11) {
            if (!selection_.empty()) {
                // Toolbar actions are handled on mouse-down. Defer the field
                // until the corresponding mouse-up has finished, otherwise
                // TextField::onDeselect immediately commits/closes it when the
                // selected note also has to be scrolled back into view.
                pendingInlineLyricNote_ = static_cast<int>(*selection_.begin());
                pendingInlineLyricDelayFrames_ = 6;
            }
        } else if (action == 12) {
            beginInspectorEdit(InspectorField::Alias);
        } else if (action == 13) {
            beginInspectorEdit(InspectorField::VibratoDepth);
        } else if (action == 27) {
            beginInspectorEdit(InspectorField::Position);
        } else if (action == 14) {
            addSection();
        } else if (action == 15) {
            editSectionBounds();
        } else if (action == 16) {
            renameSection();
        } else if (action == 17) {
            deleteSection();
        } else if (action == 18) {
            module_->undo();
        } else if (action == 19) {
            module_->redo();
        } else if (action == 20) {
            module_->editorFollowPlayhead = false;
            zoomFull();
        } else if (action == 21) {
            module_->editorFollowPlayhead = false;
            zoomSection();
        } else if (action == 22) {
            requestDelete();
        } else if (action == 23) {
            insertingLyric_ = !insertingLyric_;
            mode_ = EditMode::Notes;
        } else if (action == 28) {
            module_->panelPlaying.store(!module_->panelPlaying.load(std::memory_order_relaxed),
                                        std::memory_order_relaxed);
        } else if (action == 29) {
            module_->editorResetRequested.store(true, std::memory_order_release);
        } else if (action == 30) {
            const bool enabled = module_->params[VocalModule::LOOP_PARAM].getValue() >= 0.5f;
            module_->params[VocalModule::LOOP_PARAM].setValue(enabled ? 0.f : 1.f);
        } else if (action == 31) {
            const bool section = module_->params[VocalModule::RANGE_PARAM].getValue() >= 0.5f;
            module_->params[VocalModule::RANGE_PARAM].setValue(section ? 0.f : 1.f);
        } else if (action == 24) {
            module_->phonemizerName = kJapaneseAutoPhonemizer;
            module_->replaceScore(makeDroneScore(), "New Japanese sustained vowel instrument");
            module_->params[VocalModule::LOOP_PARAM].setValue(1.f);
            module_->params[VocalModule::RANGE_PARAM].setValue(0.f);
            module_->panelPlaying.store(true);
            selection_.clear();
            clearCurvePointSelection();
            fitPitchRange();
            zoomFull();
        } else if (action == 25) {
            module_->phonemizerName = kJapaneseAutoPhonemizer;
            module_->replaceScore(makeTriggeredWordScore(), "New Japanese triggered word");
            module_->params[VocalModule::LOOP_PARAM].setValue(0.f);
            module_->params[VocalModule::RANGE_PARAM].setValue(0.f);
            module_->panelPlaying.store(false);
            selection_.clear();
            clearCurvePointSelection();
            fitPitchRange();
            zoomFull();
        } else if (action == 26) {
            module_->phonemizerName = kJapaneseAutoPhonemizer;
            module_->replaceScore(makeLoopPhraseScore(), "New Japanese looping phrase with rest");
            module_->params[VocalModule::LOOP_PARAM].setValue(1.f);
            module_->params[VocalModule::RANGE_PARAM].setValue(1.f);
            module_->params[VocalModule::SECTION_PARAM].setValue(0.f);
            module_->panelPlaying.store(true);
            selection_.clear();
            clearCurvePointSelection();
            fitPitchRange();
            zoomFull();
        } else if (action == 44) {
            module_->phonemizerName = kJapaneseAutoPhonemizer;
            module_->replaceScore(makeJapaneseFirstSoundScore(), "New Japanese first-sound phrase");
            module_->params[VocalModule::LOOP_PARAM].setValue(0.f);
            module_->params[VocalModule::RANGE_PARAM].setValue(0.f);
            module_->panelPlaying.store(true);
            selection_.clear();
            clearCurvePointSelection();
            fitPitchRange();
            zoomFull();
        } else if (action >= 45 && action <= 47) {
            module_->phonemizerName = module_->singerId == "builtin:adachi-rei"
                ? kEnglishToJapanesePhonemizer : kEnglishXSampaPhonemizer;
            if (action == 45) {
                module_->replaceScore(makeEnglishDroneScore(), "New English sustained vowel instrument");
                module_->params[VocalModule::LOOP_PARAM].setValue(1.f);
                module_->params[VocalModule::RANGE_PARAM].setValue(0.f);
                module_->panelPlaying.store(true);
            } else if (action == 46) {
                module_->replaceScore(makeEnglishTriggeredWordScore(), "New English triggered word");
                module_->params[VocalModule::LOOP_PARAM].setValue(0.f);
                module_->params[VocalModule::RANGE_PARAM].setValue(0.f);
                module_->panelPlaying.store(false);
            } else {
                module_->replaceScore(makeEnglishLoopPhraseScore(), "New English looping phrase with rest");
                module_->params[VocalModule::LOOP_PARAM].setValue(1.f);
                module_->params[VocalModule::RANGE_PARAM].setValue(1.f);
                module_->params[VocalModule::SECTION_PARAM].setValue(0.f);
                module_->panelPlaying.store(true);
            }
            selection_.clear();
            clearCurvePointSelection();
            fitPitchRange();
            zoomFull();
        } else if (action == 32) {
            openFileMenu();
        } else if (action == 33) {
            openEditMenu();
        } else if (action == 35) {
            resetSelectedVoiceShaping();
        } else if (action == 36) {
            module_->editorFollowPlayhead = !module_->editorFollowPlayhead;
        } else if (action == 37) {
            openViewMenu();
        } else if (action == 38) {
            beginInspectorEdit(InspectorField::Tone);
        } else if (action == 39) {
            beginInspectorEdit(InspectorField::Start);
        }
    } catch (const std::exception& error) {
        osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, error.what());
    }
}

void importVocalScoreFile(VocalModule* module) {
    if (!module) return;
    auto* filters = osdialog_filters_parse(kVocalScoreDialogFilterSpec);
    char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
    osdialog_filters_free(filters);
    if (!path) return;
    const std::string filename(path);
    std::free(path);

    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".vocalrack") {
        module->loadProject(loadProjectFile(filename));
        module->lastImportReport = "Loaded lossless VocalRack project: " + filename;
        return;
    }

    UstxImporter importer;
    auto tracks = importer.scanTracks(filename);
    if (tracks.empty()) throw std::runtime_error("The selected file contains no vocal note tracks");
    int selected = tracks.front().index;
    if (tracks.size() > 1) {
        std::ostringstream choices;
        for (const auto& track : tracks) choices << track.index << ": " << track.name << " (" << track.noteCount << " notes)\n";
        const auto answer = prompt(("Select vocal track index:\n" + choices.str()).c_str(), std::to_string(selected));
        if (!answer || answer->empty()) return;
        selected = std::stoi(*answer);
    }
    auto result = importer.importTrack(filename, selected);
    std::ostringstream report;
    for (const auto& message : result.report.imported) report << "Imported: " << message << '\n';
    for (const auto& message : result.report.approximated) report << "Approximated: " << message << '\n';
    for (const auto& message : result.report.ignored) report << "Ignored: " << message << '\n';
    for (const auto& message : result.report.warnings) report << "Warning: " << message << '\n';
    module->lastImportReport = report.str();
    module->replaceScore(std::move(result.score), "Import vocal score");
    const bool needsAttention = !result.report.warnings.empty() || !result.report.ignored.empty();
    if (needsAttention) module->status.store(ModuleStatus::ImportWarning);
    osdialog_message(needsAttention ? OSDIALOG_WARNING : OSDIALOG_INFO, OSDIALOG_OK,
                     module->lastImportReport.c_str());
}

namespace {

std::string withExtension(std::string path, const std::string& extension) {
    if (path.size() < extension.size() || path.substr(path.size() - extension.size()) != extension)
        path += extension;
    return path;
}

}  // namespace

void saveVocalProjectFile(VocalModule* module) {
    if (!module) return;
    auto* filters = osdialog_filters_parse(kVocalProjectDialogFilterSpec);
    char* selected = osdialog_file(OSDIALOG_SAVE, nullptr, "song.vocalrack", filters);
    osdialog_filters_free(filters);
    if (!selected) return;
    const std::string path = withExtension(selected, ".vocalrack");
    std::free(selected);
    saveProjectFile(path, module->captureProject());
    module->lastImportReport = "Saved lossless VocalRack project: " + path;
}

void exportVocalUstxFile(VocalModule* module) {
    if (!module) return;
    auto* filters = osdialog_filters_parse(kUstxExportDialogFilterSpec);
    char* selected = osdialog_file(OSDIALOG_SAVE, nullptr, "song.ustx", filters);
    osdialog_filters_free(filters);
    if (!selected) return;
    const std::string path = withExtension(selected, ".ustx");
    std::free(selected);
    UstxExportOptions options;
    options.trackName = module->score.title.empty() ? "VocalRack voice" : module->score.title;
    options.singer = module->singerId == "builtin:adachi-rei" ? "adachi-rei" : module->singerDisplayName();
    options.phonemizer = module->phonemizerName == kJapaneseAutoPhonemizer
        ? "OpenUtau.Plugin.Builtin.JapanesePresampPhonemizer"
        : module->phonemizerName == kJapaneseCvvcPhonemizer
        ? "OpenUtau.Plugin.Builtin.JapaneseCVVCPhonemizer"
        : module->phonemizerName == kEnglishToJapanesePhonemizer
        ? "OpenUtau.Plugin.Builtin.ENtoJAPhonemizer"
        : module->phonemizerName == kEnglishXSampaPhonemizer
        ? "OpenUtau.Plugin.Builtin.EnXSampaPhonemizer"
        : module->phonemizerName == kEnglishVccvPhonemizer
        ? "OpenUtau.Plugin.Builtin.EnglishVCCVPhonemizer" : std::string();
    const auto exported = exportUstx(module->score, options);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create OpenUtau USTX: " + path);
    output << exported.text;
    if (!output) throw std::runtime_error("Cannot finish writing OpenUtau USTX: " + path);
    std::ostringstream report;
    report << "Exported OpenUtau USTX: " << path << '\n';
    for (const auto& item : exported.preserved) report << "Preserved: " << item << '\n';
    for (const auto& item : exported.approximated) report << "Approximated: " << item << '\n';
    for (const auto& item : exported.nativeOnly) report << "Native project only: " << item << '\n';
    module->lastImportReport = report.str();
}

void VocalEditor::importUstx() {
    importVocalScoreFile(module_);
    selection_.clear();
    clearCurvePointSelection();
    fitPitchRange();
    zoomFull();
}

void VocalEditor::selectSinger() {
    char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
    if (!path) return;
    module_->selectSingerFolder(path);
    std::free(path);
}

void VocalEditor::beginInlineLyric(size_t noteIndex) {
    if (noteIndex >= module_->score.notes.size()) return;
    if (inlineLyricField_) finishInlineLyric(true);
    // A selection can survive a jump or scrollbar move. Bring the target note
    // back into view before placing the inline field so LYRIC/Return never
    // opens an editor invisibly off-screen.
    ensureNoteVisible(noteIndex);
    inlineLyricNoteIndex_ = noteIndex;
    inlineLyricBefore_ = scoreToJson(module_->score);
    auto* field = new InlineLyricField;
    inlineLyricField_ = field;
    field->placeholder = isJapanesePhonemizer(module_->phonemizerName)
        ? "kana or romaji (da becomes だ)" : "English word or hint [r i d]";
    field->setText(module_->score.notes[noteIndex].lyric);
    field->selectAll();
    field->onFinish = [this](bool commit, int advance) { finishInlineLyric(commit, advance); };
    addChild(field);
    updateInlineLyricLayout();
    APP->event->setSelectedWidget(field);
}

void VocalEditor::finishInlineLyric(bool commit, int advance) {
    auto* field = inlineLyricField_;
    if (!field) return;
    const size_t noteIndex = inlineLyricNoteIndex_;
    const std::string entered = field->getText();
    if (commit && isJapanesePhonemizer(module_->phonemizerName) &&
        !validJapaneseLyricInput(entered)) {
        field->closing = false;
        field->invalid = true;
        inspectorError_ = "Romaji not recognized. Use kana or a spelling such as da, chi, or re";
        APP->event->setSelectedWidget(field);
        return;
    }
    inlineLyricField_ = nullptr;
    inlineLyricNoteIndex_ = std::numeric_limits<size_t>::max();
    field->closing = true;
    field->requestDelete();
    if (commit && !entered.empty() && noteIndex < module_->score.notes.size()) {
        const std::string lyric = isJapanesePhonemizer(module_->phonemizerName)
            ? normalizeJapanese(entered) : entered;
        if (!lyric.empty() && lyric != module_->score.notes[noteIndex].lyric) {
            module_->score.notes[noteIndex].lyric = lyric;
            module_->score.notes[noteIndex].aliasOverride.reset();
            module_->score.notes[noteIndex].phonemeOverrides.clear();
            module_->commitScoreEdit(inlineLyricBefore_, "Edit lyric inline");
        }
        inspectorError_.clear();
    }

    if (commit && advance != 0 && !module_->score.notes.empty()) {
        const auto current = std::min(noteIndex, module_->score.notes.size() - 1);
        const long long next = static_cast<long long>(current) + advance;
        if (next >= 0 && next < static_cast<long long>(module_->score.notes.size())) {
            selection_.clear();
            selection_.insert(static_cast<size_t>(next));
            beginInlineLyric(static_cast<size_t>(next));
            return;
        }
    }
    // Deleting the inline TextField otherwise leaves Rack's selected-widget
    // pointer on a closing child. Return focus to the editor so Esc, undo,
    // copy/paste, Insert Lyric, and other keyboard commands keep working
    // immediately after Return, Escape, or the final Tab in a lyric run.
    APP->event->setSelectedWidget(this);
}

void VocalEditor::updateInlineLyricLayout() {
    if (!inlineLyricField_) return;
    if (inlineLyricNoteIndex_ >= module_->score.notes.size()) {
        finishInlineLyric(false);
        return;
    }
    auto rect = noteRect(module_->score.notes[inlineLyricNoteIndex_]);
    rect.pos.y -= 2.f;
    rect.size.y = std::max(28.f, rect.size.y + 4.f);
    rect.size.x = std::max(112.f, rect.size.x);
    rect.pos.x = std::clamp(rect.pos.x, piano_.pos.x,
                            std::max(piano_.pos.x, piano_.pos.x + piano_.size.x - rect.size.x));
    inlineLyricField_->box = rect;
}

std::string VocalEditor::inspectorValue(InspectorField field) const {
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return {};
    const auto& note = module_->score.notes[*selection_.begin()];
    std::ostringstream value;
    switch (field) {
        case InspectorField::Lyric: return note.lyric;
        case InspectorField::Alias: {
            if (!note.phonemeOverrides.empty()) return joinPhonemes(note.phonemeOverrides);
            if (note.aliasOverride) return "EXACT: " + *note.aliasOverride;
            const auto diagnostics = module_->renderSlot->copyDiagnostics();
            std::vector<const PhonemeEvent*> phones;
            for (const auto& phone : diagnostics.phonemes)
                if (phone.sourceNoteId == note.id) phones.push_back(&phone);
            std::stable_sort(phones.begin(), phones.end(), [](const auto* left, const auto* right) {
                return left->relativeTick < right->relativeTick;
            });
            if (phones.empty()) return "AUTO";
            std::vector<std::string> aliases;
            aliases.reserve(phones.size());
            for (const auto* phone : phones)
                aliases.push_back(!phone->selectedAlias.empty() ? phone->selectedAlias
                                                                 : phone->requestedAlias);
            return "AUTO: " + joinPhonemes(aliases);
        }
        case InspectorField::Tone: return noteName(note.midiNote);
        case InspectorField::Start: return formatMusicalPosition(module_->score, note.startTick);
        case InspectorField::Length:
            value << std::fixed << std::setprecision(2)
                  << note.durationTick / static_cast<double>(ticksPerBeat(module_->score));
            return value.str();
        case InspectorField::Position: value << note.phonemeTiming.positionOffsetTick.value_or(0); break;
        case InspectorField::Preutterance: value << std::lround(note.phonemeTiming.preutteranceDeltaMs.value_or(0.f)); break;
        case InspectorField::Overlap: value << std::lround(note.phonemeTiming.overlapDeltaMs.value_or(0.f)); break;
        case InspectorField::Attack: value << std::lround(note.phonemeTiming.attackTimeDeltaMs.value_or(0.f)); break;
        case InspectorField::Release: value << std::lround(note.phonemeTiming.releaseTimeDeltaMs.value_or(0.f)); break;
        case InspectorField::VibratoEnabled: return note.vibrato.depthCents > 0.f ? "ON" : "OFF";
        case InspectorField::VibratoStart: value << std::lround(note.vibrato.startPercent); break;
        case InspectorField::VibratoDepth: value << std::lround(note.vibrato.depthCents); break;
        case InspectorField::VibratoRate:
            value << std::fixed << std::setprecision(1) << note.vibrato.rateHz;
            break;
        default: return {};
    }
    return value.str();
}

bool VocalEditor::applyInspectorValue(InspectorField field, const std::string& entered) {
    if (inspectorEditNoteIndex_ >= module_->score.notes.size()) return false;
    auto& note = module_->score.notes[inspectorEditNoteIndex_];
    auto parseFloat = [&](float& result) {
        try {
            size_t used = 0;
            result = std::stof(entered, &used);
            return used == entered.size() && std::isfinite(result);
        } catch (...) {
            return false;
        }
    };
    auto optionalFloat = [](float number) -> std::optional<float> {
        return std::abs(number) < 0.0001f ? std::nullopt : std::optional<float>(number);
    };
    if (field == InspectorField::Lyric) {
        if (isJapanesePhonemizer(module_->phonemizerName) &&
            !validJapaneseLyricInput(entered)) {
            inspectorError_ = "Romaji not recognized. Use kana or a spelling such as da, chi, or re";
            return false;
        }
        if (entered.empty()) { inspectorError_ = "Lyric cannot be empty"; return false; }
        const std::string lyric = isJapanesePhonemizer(module_->phonemizerName)
            ? normalizeJapanese(entered) : entered;
        if (lyric.empty()) { inspectorError_ = "Enter kana, romaji, +, -, or ー"; return false; }
        note.lyric = lyric;
        note.aliasOverride.reset();
        note.phonemeOverrides.clear();
    } else if (field == InspectorField::Alias) {
        const std::string trimmed = trimEditorText(entered);
        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (trimmed.empty() || lower == "auto" || lower.rfind("auto:", 0) == 0 ||
            trimmed == note.lyric) {
            note.aliasOverride.reset();
            note.phonemeOverrides.clear();
        } else if (lower.rfind("exact:", 0) == 0) {
            const auto exact = trimEditorText(trimmed.substr(6));
            if (exact.empty()) {
                inspectorError_ = "EXACT needs a voicebank alias";
                return false;
            }
            note.aliasOverride = exact;
            note.phonemeOverrides.clear();
        } else {
            auto aliases = splitPhonemeOverrides(trimmed);
            if (aliases.empty() || std::any_of(aliases.begin(), aliases.end(),
                [](const std::string& alias) { return alias.empty(); })) {
                inspectorError_ = "Separate phonemes with |; each entry needs an alias (example: su | ta | u)";
                return false;
            }
            for (auto& alias : aliases) {
                // Simple romaji tokens are author-friendly for a Japanese
                // bank. Preserve aliases containing spaces/dashes exactly so
                // users can still enter VCV forms such as `a う` or `- す`.
                if (usesJapaneseAliases(module_->phonemizerName) &&
                    alias.find_first_of(" \t-") == std::string::npos) {
                    const auto normalized = normalizeJapanese(alias);
                    if (!normalized.empty()) alias = normalized;
                }
            }
            note.aliasOverride.reset();
            note.phonemeOverrides = std::move(aliases);
        }
    } else if (field == InspectorField::Tone) {
        const auto midi = parseMidiTone(entered);
        if (!midi) { inspectorError_ = "Tone example: C#4 or MIDI 61"; return false; }
        note.midiNote = *midi;
        module_->auditionMidiNote(*midi);
    } else if (field == InspectorField::Start) {
        const auto tick = parseMusicalPosition(module_->score, entered);
        if (!tick) { inspectorError_ = "Start example: 2:1 or 2:1+60"; return false; }
        note.startTick = *tick;
    } else if (field == InspectorField::Length) {
        float beats = 0.f;
        if (!parseFloat(beats) || beats <= 0.f) { inspectorError_ = "Length is a positive number of beats"; return false; }
        note.durationTick = std::max<int64_t>(1,
            static_cast<int64_t>(std::llround(beats * ticksPerBeat(module_->score))));
    } else if (field == InspectorField::Position) {
        try {
            size_t used = 0;
            const int64_t ticks = std::stoll(entered, &used);
            if (used != entered.size()) throw std::invalid_argument("trailing");
            note.phonemeTiming.positionOffsetTick = ticks == 0 ? std::nullopt
                : std::optional<int64_t>(std::clamp<int64_t>(ticks, -1920, 1920));
        } catch (...) { inspectorError_ = "Position is a whole tick offset"; return false; }
    } else {
        float number = 0.f;
        if (!parseFloat(number)) { inspectorError_ = "Enter a numeric value"; return false; }
        if (field == InspectorField::Preutterance)
            note.phonemeTiming.preutteranceDeltaMs = optionalFloat(std::clamp(number, -500.f, 500.f));
        else if (field == InspectorField::Overlap)
            note.phonemeTiming.overlapDeltaMs = optionalFloat(std::clamp(number, -500.f, 500.f));
        else if (field == InspectorField::Attack)
            note.phonemeTiming.attackTimeDeltaMs = optionalFloat(std::clamp(number, -500.f, 500.f));
        else if (field == InspectorField::Release)
            note.phonemeTiming.releaseTimeDeltaMs = optionalFloat(std::clamp(number, -500.f, 500.f));
        else if (field == InspectorField::VibratoStart)
            note.vibrato.startPercent = std::clamp(number, 0.f, 100.f);
        else if (field == InspectorField::VibratoDepth)
            note.vibrato.depthCents = std::clamp(number, 0.f, 200.f);
        else if (field == InspectorField::VibratoRate)
            note.vibrato.rateHz = std::clamp(number, 0.1f, 20.f);
    }
    module_->score.normalize();
    const auto errors = module_->score.validate();
    if (!errors.empty()) {
        inspectorError_ = "Timing overlaps another note";
        return false;
    }
    return true;
}

void VocalEditor::beginInspectorEdit(InspectorField field) {
    if (field == InspectorField::None || field == InspectorField::VibratoEnabled ||
        selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return;
    if (inspectorValueField_) finishInspectorEdit(true);
    inspectorEditField_ = field;
    inspectorEditNoteIndex_ = *selection_.begin();
    inspectorEditNoteId_ = module_->score.notes[inspectorEditNoteIndex_].id;
    inspectorEditBefore_ = scoreToJson(module_->score);
    inspectorError_.clear();
    auto* fieldWidget = new InspectorValueField;
    inspectorValueField_ = fieldWidget;
    fieldWidget->placeholder = field == InspectorField::Alias ? "AUTO or su | ta | u" : "";
    fieldWidget->setText(inspectorValue(field));
    fieldWidget->selectAll();
    fieldWidget->onFinish = [this](bool commit, int advance) { finishInspectorEdit(commit, advance); };
    addChild(fieldWidget);
    updateInspectorFieldLayout();
    APP->event->setSelectedWidget(fieldWidget);
}

void VocalEditor::finishInspectorEdit(bool commit, int advance) {
    auto* fieldWidget = inspectorValueField_;
    if (!fieldWidget) return;
    const InspectorField completed = inspectorEditField_;
    const std::string entered = fieldWidget->getText();
    const std::string noteId = inspectorEditNoteId_;

    bool changed = false;
    if (commit) {
        changed = applyInspectorValue(completed, entered);
        if (!changed) {
            module_->score = scoreFromJson(inspectorEditBefore_);
            fieldWidget->closing = false;
            fieldWidget->invalid = true;
            APP->event->setSelectedWidget(fieldWidget);
            return;
        }
        module_->commitScoreEdit(inspectorEditBefore_, "Edit note inspector field");
        inspectorError_.clear();
    }
    inspectorValueField_ = nullptr;
    inspectorEditField_ = InspectorField::None;
    fieldWidget->closing = true;
    fieldWidget->requestDelete();
    const auto found = std::find_if(module_->score.notes.begin(), module_->score.notes.end(),
        [&](const Note& note) { return note.id == noteId; });
    if (found != module_->score.notes.end()) {
        selection_.clear();
        selection_.insert(static_cast<size_t>(found - module_->score.notes.begin()));
    }
    inspectorEditNoteIndex_ = std::numeric_limits<size_t>::max();
    inspectorEditNoteId_.clear();

    if (commit && changed && advance != 0) {
        static const InspectorField order[] = {
            InspectorField::Lyric, InspectorField::Alias, InspectorField::Tone,
            InspectorField::Start, InspectorField::Length, InspectorField::Position,
            InspectorField::Preutterance, InspectorField::Overlap, InspectorField::Attack,
            InspectorField::Release, InspectorField::VibratoStart,
            InspectorField::VibratoDepth, InspectorField::VibratoRate,
        };
        const auto current = std::find(std::begin(order), std::end(order), completed);
        if (current != std::end(order)) {
            const long next = static_cast<long>(current - std::begin(order)) + advance;
            const long orderSize = static_cast<long>(sizeof(order) / sizeof(order[0]));
            if (next >= 0 && next < orderSize) {
                beginInspectorEdit(order[next]);
                return;
            }
        }
    }
    APP->event->setSelectedWidget(this);
}

void VocalEditor::updateInspectorFieldLayout() {
    if (!inspectorValueField_) return;
    const auto found = std::find_if(inspectorControls_.begin(), inspectorControls_.end(),
        [&](const InspectorControl& control) { return control.field == inspectorEditField_; });
    if (found == inspectorControls_.end()) { finishInspectorEdit(false); return; }
    inspectorValueField_->box = found->value;
}

std::pair<float, float> VocalEditor::inspectorRange(InspectorField field) const {
    switch (field) {
        case InspectorField::Position: return {-240.f, 240.f};
        case InspectorField::Preutterance:
        case InspectorField::Overlap:
        case InspectorField::Attack:
        case InspectorField::Release: return {-200.f, 200.f};
        case InspectorField::VibratoStart: return {0.f, 100.f};
        case InspectorField::VibratoDepth: return {0.f, 200.f};
        case InspectorField::VibratoRate: return {0.1f, 20.f};
        default: return {0.f, 1.f};
    }
}

float VocalEditor::inspectorNumericValue(InspectorField field) const {
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return 0.f;
    const auto& note = module_->score.notes[*selection_.begin()];
    switch (field) {
        case InspectorField::Position: return static_cast<float>(note.phonemeTiming.positionOffsetTick.value_or(0));
        case InspectorField::Preutterance: return note.phonemeTiming.preutteranceDeltaMs.value_or(0.f);
        case InspectorField::Overlap: return note.phonemeTiming.overlapDeltaMs.value_or(0.f);
        case InspectorField::Attack: return note.phonemeTiming.attackTimeDeltaMs.value_or(0.f);
        case InspectorField::Release: return note.phonemeTiming.releaseTimeDeltaMs.value_or(0.f);
        case InspectorField::VibratoStart: return note.vibrato.startPercent;
        case InspectorField::VibratoDepth: return note.vibrato.depthCents;
        case InspectorField::VibratoRate: return note.vibrato.rateHz;
        default: return 0.f;
    }
}

void VocalEditor::setInspectorNumericValue(InspectorField field, float number) {
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return;
    auto& note = module_->score.notes[*selection_.begin()];
    auto optionalFloat = [](float value) -> std::optional<float> {
        return std::abs(value) < 0.05f ? std::nullopt : std::optional<float>(value);
    };
    if (field == InspectorField::Position) {
        const int64_t value = static_cast<int64_t>(std::llround(number));
        note.phonemeTiming.positionOffsetTick = value == 0 ? std::nullopt : std::optional<int64_t>(value);
    } else if (field == InspectorField::Preutterance) note.phonemeTiming.preutteranceDeltaMs = optionalFloat(number);
    else if (field == InspectorField::Overlap) note.phonemeTiming.overlapDeltaMs = optionalFloat(number);
    else if (field == InspectorField::Attack) note.phonemeTiming.attackTimeDeltaMs = optionalFloat(number);
    else if (field == InspectorField::Release) note.phonemeTiming.releaseTimeDeltaMs = optionalFloat(number);
    else if (field == InspectorField::VibratoStart) note.vibrato.startPercent = number;
    else if (field == InspectorField::VibratoDepth) note.vibrato.depthCents = number;
    else if (field == InspectorField::VibratoRate) note.vibrato.rateHz = number;
}

void VocalEditor::editLyric(bool alias) {
    if (selection_.empty()) return;
    const size_t index = *selection_.begin();
    if (index >= module_->score.notes.size()) return;
    auto& note = module_->score.notes[index];
    const auto value = prompt(alias ? "Direct alias override (blank clears it)" : "Japanese lyric",
                              alias ? note.aliasOverride.value_or("") : note.lyric);
    if (!value || (!alias && value->empty())) return;
    const auto before = scoreToJson(module_->score);
    if (alias) {
        if (value->empty()) {
            note.aliasOverride.reset();
            note.phonemeOverrides.clear();
        } else {
            note.aliasOverride.reset();
            note.phonemeOverrides = splitPhonemeOverrides(*value);
        }
    } else {
        note.lyric = isJapanesePhonemizer(module_->phonemizerName) ? normalizeJapanese(*value) : *value;
        note.aliasOverride.reset();
        note.phonemeOverrides.clear();
    }
    module_->commitScoreEdit(before, alias ? "Edit alias" : "Edit lyric");
}

void VocalEditor::editVibrato() {
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return;
    auto& note = module_->score.notes[*selection_.begin()];
    const auto value = prompt("Vibrato: start-percent depth-cents rate-Hz",
                              std::to_string(note.vibrato.startPercent) + " " +
                                  std::to_string(note.vibrato.depthCents) + " " + std::to_string(note.vibrato.rateHz));
    if (!value || value->empty()) return;
    float start, depth, rate;
    std::stringstream input(*value);
    if (input >> start >> depth >> rate) {
        const auto before = scoreToJson(module_->score);
        note.vibrato.startPercent = std::clamp(start, 0.f, 100.f);
        note.vibrato.depthCents = std::clamp(depth, 0.f, 200.f);
        note.vibrato.rateHz = std::clamp(rate, 0.1f, 20.f);
        module_->commitScoreEdit(before, "Edit vibrato");
    }
}

void VocalEditor::editPhonemeTiming() {
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return;
    auto& note = module_->score.notes[*selection_.begin()];
    const auto& timing = note.phonemeTiming;
    std::ostringstream initial;
    initial << timing.positionOffsetTick.value_or(0) << ' '
            << timing.preutteranceDeltaMs.value_or(0.f) << ' '
            << timing.overlapDeltaMs.value_or(0.f) << ' '
            << timing.attackTimeDeltaMs.value_or(0.f) << ' '
            << timing.releaseTimeDeltaMs.value_or(0.f);
    const auto value = prompt(
        "Phoneme timing: position-ticks preutter-delta-ms overlap-delta-ms attack-delta-ms release-delta-ms (all 0 = voicebank timing)",
        initial.str());
    if (!value || value->empty()) return;
    int64_t position = 0;
    float preutter = 0.f, overlap = 0.f, attack = 0.f, release = 0.f;
    std::stringstream input(*value);
    if (!(input >> position >> preutter >> overlap >> attack >> release)) return;
    const auto before = scoreToJson(module_->score);
    auto optionalInt = [](int64_t number) -> std::optional<int64_t> {
        return number == 0 ? std::nullopt : std::optional<int64_t>(number);
    };
    auto optionalFloat = [](float number) -> std::optional<float> {
        return std::abs(number) < 0.0001f ? std::nullopt : std::optional<float>(number);
    };
    note.phonemeTiming.positionOffsetTick = optionalInt(position);
    note.phonemeTiming.preutteranceDeltaMs = optionalFloat(std::clamp(preutter, -500.f, 500.f));
    note.phonemeTiming.overlapDeltaMs = optionalFloat(std::clamp(overlap, -500.f, 500.f));
    note.phonemeTiming.attackTimeDeltaMs = optionalFloat(std::clamp(attack, -500.f, 500.f));
    note.phonemeTiming.releaseTimeDeltaMs = optionalFloat(std::clamp(release, -500.f, 500.f));
    module_->score.normalize();
    module_->commitScoreEdit(before, "Edit phoneme timing");
}

void VocalEditor::resetPhonemeTiming() {
    if (selection_.empty()) return;
    const auto before = scoreToJson(module_->score);
    for (const auto index : selection_)
        if (index < module_->score.notes.size()) module_->score.notes[index].phonemeTiming = {};
    module_->commitScoreEdit(before, "Reset phoneme timing");
}

void VocalEditor::resetSelectedVoiceShaping() {
    if (selection_.empty()) return;
    bool changed = false;
    const auto before = scoreToJson(module_->score);
    for (const auto index : selection_) {
        if (index >= module_->score.notes.size()) continue;
        auto& note = module_->score.notes[index];
        const auto& timing = note.phonemeTiming;
        changed = changed || note.aliasOverride.has_value() || !note.phonemeOverrides.empty() ||
                  !note.pitchSnapFirst ||
                  !note.pitchCents.points.empty() ||
                  !note.dynamicsDb.points.empty() || note.vibrato.depthCents != 0.f ||
                  note.vibrato.startPercent != Vibrato{}.startPercent ||
                  note.vibrato.rateHz != Vibrato{}.rateHz || note.vibrato.phase != 0.f ||
                  note.vibrato.fadeInPercent != Vibrato{}.fadeInPercent ||
                  note.vibrato.fadeOutPercent != Vibrato{}.fadeOutPercent ||
                  timing.positionOffsetTick.has_value() || timing.preutteranceDeltaMs.has_value() ||
                  timing.overlapDeltaMs.has_value() || timing.attackTimeDeltaMs.has_value() ||
                  timing.releaseTimeDeltaMs.has_value();
        note.aliasOverride.reset();
        note.phonemeOverrides.clear();
        note.pitchSnapFirst = true;
        note.pitchCents.points.clear();
        note.dynamicsDb.points.clear();
        note.vibrato = {};
        note.phonemeTiming = {};
    }
    clearCurvePointSelection();
    if (changed) module_->commitScoreEdit(before, "Restore voice shaping defaults");
}

void VocalEditor::editNoteTone() {
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return;
    auto& note = module_->score.notes[*selection_.begin()];
    const auto value = prompt("Tone: note name or MIDI number (examples: C#4 or 61)", noteName(note.midiNote));
    if (!value || value->empty()) return;
    const auto midi = parseMidiTone(*value);
    if (!midi) throw std::runtime_error("Enter a note name such as C#4, or a MIDI number from 0 to 127");
    if (*midi == note.midiNote) return;
    const auto before = scoreToJson(module_->score);
    note.midiNote = *midi;
    module_->commitScoreEdit(before, "Edit note tone");
    module_->auditionMidiNote(*midi);
}

void VocalEditor::editNoteTiming() {
    if (selection_.empty() || *selection_.begin() >= module_->score.notes.size()) return;
    auto& note = module_->score.notes[*selection_.begin()];
    std::ostringstream initial;
    initial << formatMusicalPosition(module_->score, note.startTick) << ' '
            << std::fixed << std::setprecision(3)
            << note.durationTick / static_cast<double>(ticksPerBeat(module_->score));
    const auto value = prompt("Timing: start as bar:beat[+ticks], then length in beats", initial.str());
    if (!value || value->empty()) return;
    std::string startText;
    double durationBeats = 0.0;
    std::stringstream input(*value);
    if (!(input >> startText >> durationBeats) || durationBeats <= 0.0)
        throw std::runtime_error("Use a value such as 2:1 1.5 (bar 2 beat 1, length 1.5 beats)");
    const auto start = parseMusicalPosition(module_->score, startText);
    if (!start) throw std::runtime_error("Start must use bar:beat or bar:beat+ticks, for example 2:1+60");
    const auto before = scoreToJson(module_->score);
    note.startTick = *start;
    note.durationTick = std::max<int64_t>(1,
        static_cast<int64_t>(std::llround(durationBeats * ticksPerBeat(module_->score))));
    module_->score.normalize();
    const auto errors = module_->score.validate();
    if (!errors.empty()) {
        module_->score = scoreFromJson(before);
        throw std::runtime_error("That timing overlaps another monophonic note: " + errors.front());
    }
    module_->commitScoreEdit(before, "Edit note timing");
}

void VocalEditor::sliceNoteAt(size_t noteIndex, int64_t tick) {
    if (noteIndex >= module_->score.notes.size()) return;
    const int64_t minimumDuration = module_->editorSnapEnabled
        ? std::max<int64_t>(1, module_->editorSnapTick) : 1;
    auto& original = module_->score.notes[noteIndex];
    const int64_t splitTick = snapTick(tick);
    if (splitTick < original.startTick + minimumDuration ||
        splitTick > original.endTick() - minimumDuration)
        return;

    const auto before = scoreToJson(module_->score);
    const int64_t oldEnd = original.endTick();
    const int64_t splitOffset = splitTick - original.startTick;
    Note continuation = original;
    continuation.id = makeUuid();
    const std::string continuationId = continuation.id;
    continuation.startTick = splitTick;
    continuation.durationTick = oldEnd - splitTick;
    continuation.lyric = "+";
    continuation.aliasOverride.reset();
    continuation.phonemeOverrides.clear();
    continuation.phonemeTiming = {};

    auto splitCurve = [&](Curve& left, Curve& right) {
        if (left.points.empty()) { right.points.clear(); return; }
        const Curve authored = left;
        const float valueAtSplit = authored.sample(splitOffset);
        left.points.erase(std::remove_if(left.points.begin(), left.points.end(),
                                         [&](const CurvePoint& point) { return point.tickOffset > splitOffset; }),
                          left.points.end());
        left.points.push_back({splitOffset, valueAtSplit});
        right.points.clear();
        right.points.push_back({0, valueAtSplit});
        for (const auto& point : authored.points)
            if (point.tickOffset > splitOffset)
                right.points.push_back({point.tickOffset - splitOffset, point.value});
        left.normalize();
        right.normalize();
    };
    splitCurve(original.pitchCents, continuation.pitchCents);
    splitCurve(original.dynamicsDb, continuation.dynamicsDb);
    if (!continuation.pitchCents.points.empty()) continuation.pitchSnapFirst = false;
    original.durationTick = splitOffset;
    module_->score.notes.push_back(std::move(continuation));
    module_->score.normalize();
    selection_.clear();
    const auto found = std::find_if(module_->score.notes.begin(), module_->score.notes.end(),
        [&](const Note& candidate) { return candidate.id == continuationId; });
    if (found != module_->score.notes.end())
        selection_.insert(static_cast<size_t>(found - module_->score.notes.begin()));
    module_->commitScoreEdit(before, "Slice note");
}

void VocalEditor::insertLyricAt(rack::math::Vec pos) {
    const auto lyric = prompt("Lyric for inserted note",
        isJapanesePhonemizer(module_->phonemizerName) ? "あ" : "a");
    if (!lyric || lyric->empty()) return;
    const std::string normalizedLyric = isJapanesePhonemizer(module_->phonemizerName)
        ? normalizeJapanese(*lyric) : *lyric;
    if (normalizedLyric.empty()) return;

    const int64_t requestedTick = snapTick(xToTick(pos.x));
    const auto before = scoreToJson(module_->score);
    std::string insertedId;
    bool splitExisting = false;

    size_t containing = module_->score.notes.size();
    for (size_t i = 0; i < module_->score.notes.size(); ++i) {
        const auto& note = module_->score.notes[i];
        if (requestedTick > note.startTick && requestedTick < note.endTick()) {
            containing = i;
            break;
        }
    }

    if (containing < module_->score.notes.size()) {
        splitExisting = true;
        auto& original = module_->score.notes[containing];
        const int64_t minimumDuration = module_->editorSnapEnabled
            ? std::max<int64_t>(1, module_->editorSnapTick) : 1;
        const int64_t splitTick = std::clamp(requestedTick, original.startTick + minimumDuration,
                                             original.endTick() - minimumDuration);
        if (splitTick <= original.startTick || splitTick >= original.endTick())
            throw std::runtime_error("The selected note is too short to split at the current snap grid");

        const int64_t splitOffset = splitTick - original.startTick;
        Note inserted = original;
        inserted.id = makeUuid();
        insertedId = inserted.id;
        inserted.startTick = splitTick;
        inserted.durationTick = original.endTick() - splitTick;
        inserted.lyric = normalizedLyric;
        inserted.aliasOverride.reset();
        inserted.phonemeOverrides.clear();
        inserted.phonemeTiming = {};

        auto splitCurve = [&](Curve& left, Curve& right) {
            if (left.points.empty()) { right.points.clear(); return; }
            const Curve authored = left;
            const float valueAtSplit = authored.sample(splitOffset);
            left.points.erase(std::remove_if(left.points.begin(), left.points.end(),
                                             [&](const CurvePoint& point) { return point.tickOffset > splitOffset; }),
                              left.points.end());
            left.points.push_back({splitOffset, valueAtSplit});
            right.points.clear();
            right.points.push_back({0, valueAtSplit});
            for (const auto& point : authored.points) {
                if (point.tickOffset > splitOffset)
                    right.points.push_back({point.tickOffset - splitOffset, point.value});
            }
            left.normalize();
            right.normalize();
        };
        splitCurve(original.pitchCents, inserted.pitchCents);
        splitCurve(original.dynamicsDb, inserted.dynamicsDb);
        if (!inserted.pitchCents.points.empty()) inserted.pitchSnapFirst = false;
        original.durationTick = splitOffset;
        module_->score.notes.push_back(std::move(inserted));
    } else {
        for (const auto& note : module_->score.notes) {
            if (requestedTick >= note.startTick && requestedTick < note.endTick())
                throw std::runtime_error("That time is occupied by another note; click inside it to split the note instead");
        }
        int64_t nextStart = std::numeric_limits<int64_t>::max();
        int64_t previousEnd = 0;
        for (const auto& note : module_->score.notes) {
            if (note.endTick() <= requestedTick) previousEnd = std::max(previousEnd, note.endTick());
            if (note.startTick >= requestedTick) nextStart = std::min(nextStart, note.startTick);
        }
        const int64_t startTick = std::max(requestedTick, previousEnd);
        const int64_t minimumDuration = module_->editorSnapEnabled
            ? std::max<int64_t>(1, module_->editorSnapTick) : 1;
        int64_t duration = std::max<int64_t>(module_->editorSnapTick * 2, kTicksPerQuarter / 2);
        if (nextStart != std::numeric_limits<int64_t>::max()) duration = std::min(duration, nextStart - startTick);
        if (duration < minimumDuration)
            throw std::runtime_error("There is not enough room at this snap grid to insert a note");
        Note inserted;
        inserted.id = makeUuid();
        insertedId = inserted.id;
        inserted.startTick = startTick;
        inserted.durationTick = duration;
        inserted.midiNote = yToMidi(pos.y);
        inserted.lyric = normalizedLyric;
        module_->score.notes.push_back(std::move(inserted));
    }

    module_->score.normalize();
    const auto errors = module_->score.validate();
    if (!errors.empty()) {
        module_->score = scoreFromJson(before);
        throw std::runtime_error(errors.front());
    }
    selection_.clear();
    const auto found = std::find_if(module_->score.notes.begin(), module_->score.notes.end(),
                                    [&](const Note& note) { return note.id == insertedId; });
    if (found != module_->score.notes.end()) selection_.insert(static_cast<size_t>(found - module_->score.notes.begin()));
    insertingLyric_ = false;
    module_->commitScoreEdit(before, splitExisting ? "Split note and insert lyric" : "Insert lyric note");
}

void VocalEditor::addSection() {
    if (selection_.empty()) return;
    int64_t start = std::numeric_limits<int64_t>::max();
    int64_t end = 0;
    for (auto index : selection_) {
        if (index >= module_->score.notes.size()) continue;
        start = std::min(start, module_->score.notes[index].startTick);
        end = std::max(end, module_->score.notes[index].endTick());
    }
    if (start == std::numeric_limits<int64_t>::max()) return;
    const auto name = prompt("Section name", "SECTION");
    if (!name || name->empty()) return;
    const auto before = scoreToJson(module_->score);
    module_->score.sections.push_back({makeUuid(), *name, start, end});
    module_->score.normalize();
    const auto errors = module_->score.validate();
    if (!errors.empty()) {
        module_->score = scoreFromJson(before);
        throw std::runtime_error(errors.front());
    }
    module_->commitScoreEdit(before, "Add section");
}

void VocalEditor::editSectionBounds() {
    if (module_->score.sections.empty()) return;
    const size_t index = std::min<size_t>(std::lround(module_->params[VocalModule::SECTION_PARAM].getValue()),
                                          module_->score.sections.size() - 1);
    auto& section = module_->score.sections[index];
    const auto value = prompt("Section range: start and end as bar:beat (example: 1:1 3:1)",
                              formatMusicalPosition(module_->score, section.startTick) + " " +
                                  formatMusicalPosition(module_->score, section.endTick));
    if (!value || value->empty()) return;
    std::string startText, endText;
    std::stringstream input(*value);
    if (!(input >> startText >> endText))
        throw std::runtime_error("Enter two musical positions, for example: 1:1 3:1");
    const auto start = parseMusicalPosition(module_->score, startText);
    const auto end = parseMusicalPosition(module_->score, endText);
    if (!start || !end)
        throw std::runtime_error("Use 1-based bar:beat positions, for example: 1:1 3:1");
    const auto before = scoreToJson(module_->score);
    section.startTick = *start;
    section.endTick = *end;
    module_->score.normalize();
    const auto errors = module_->score.validate();
    if (!errors.empty()) {
        module_->score = scoreFromJson(before);
        throw std::runtime_error(errors.front());
    }
    module_->commitScoreEdit(before, "Move section boundaries");
}

void VocalEditor::renameSection() {
    if (module_->score.sections.empty()) return;
    const size_t index = std::min<size_t>(std::lround(module_->params[VocalModule::SECTION_PARAM].getValue()),
                                          module_->score.sections.size() - 1);
    const auto name = prompt("Section name", module_->score.sections[index].name);
    if (!name || name->empty()) return;
    const auto before = scoreToJson(module_->score);
    module_->score.sections[index].name = *name;
    module_->commitScoreEdit(before, "Rename section");
}

void VocalEditor::deleteSection() {
    if (module_->score.sections.empty()) return;
    const size_t index = std::min<size_t>(std::lround(module_->params[VocalModule::SECTION_PARAM].getValue()),
                                          module_->score.sections.size() - 1);
    const auto before = scoreToJson(module_->score);
    module_->score.sections.erase(module_->score.sections.begin() + index);
    module_->commitScoreEdit(before, "Delete section");
}

void VocalEditor::fitPitchRange() {
    if (module_->score.notes.empty()) return;
    int minMidi = module_->score.notes.front().midiNote;
    int maxMidi = minMidi;
    for (const auto& note : module_->score.notes) {
        minMidi = std::min(minMidi, note.midiNote);
        maxMidi = std::max(maxMidi, note.midiNote);
    }
    // Keep every imported or replacement note in view while retaining enough
    // pitch context and row height for readable lyrics and mouse targets.
    const int visibleRows = std::max(9, maxMidi - minMidi + 4);
    module_->editorZoomY = std::clamp(piano_.size.y / visibleRows, 11.f, 32.f);
    const float centerMidi = (minMidi + maxMidi) * 0.5f;
    module_->editorScrollY = piano_.size.y * 0.5f - (84.f - centerMidi) * module_->editorZoomY;
}

void VocalEditor::ensureNoteVisible(size_t noteIndex) {
    if (noteIndex >= module_->score.notes.size()) return;
    const auto& note = module_->score.notes[noteIndex];
    const auto rect = noteRect(note);
    const float horizontalMargin = 32.f;
    const float verticalMargin = std::max(8.f, module_->editorZoomY);
    if (rect.pos.x + rect.size.x < piano_.pos.x + horizontalMargin ||
        rect.pos.x > piano_.pos.x + piano_.size.x - horizontalMargin) {
        const float visibleTicks = piano_.size.x / std::max(0.02f, module_->editorZoomX);
        module_->editorScrollX = static_cast<float>(note.startTick) - visibleTicks * 0.25f;
        const float minimumScroll = -static_cast<float>(kTicksPerQuarter);
        const float maximumScroll = std::max(minimumScroll,
            static_cast<float>(module_->score.endTick() + kTicksPerQuarter) - visibleTicks);
        module_->editorScrollX = std::clamp(module_->editorScrollX, minimumScroll, maximumScroll);
    }
    if (rect.pos.y + rect.size.y < piano_.pos.y + verticalMargin ||
        rect.pos.y > piano_.pos.y + piano_.size.y - verticalMargin) {
        module_->editorScrollY = piano_.size.y * 0.5f -
            (84.f - static_cast<float>(note.midiNote)) * module_->editorZoomY;
    }
}

void VocalEditor::zoomFull() {
    // A small musical pre-roll keeps the first phoneme's preutterance handle
    // visible instead of clipping it against the editor's left edge.
    const int64_t preRoll = std::min<int64_t>(kTicksPerQuarter / 2,
        std::max<int64_t>(60, module_->score.endTick() / 20));
    module_->editorScrollX = -static_cast<float>(preRoll);
    module_->editorZoomX = std::clamp((piano_.size.x - 20.f) /
        std::max<int64_t>(1, module_->score.endTick() + preRoll), 0.02f, 2.f);
}

void VocalEditor::zoomSelection() {
    if (selection_.empty() || module_->score.notes.empty()) return;
    int64_t firstTick = std::numeric_limits<int64_t>::max();
    int64_t lastTick = std::numeric_limits<int64_t>::min();
    int minMidi = 127;
    int maxMidi = 0;
    for (const auto index : selection_) {
        if (index >= module_->score.notes.size()) continue;
        const auto& note = module_->score.notes[index];
        firstTick = std::min(firstTick, note.startTick);
        lastTick = std::max(lastTick, note.endTick());
        minMidi = std::min(minMidi, note.midiNote);
        maxMidi = std::max(maxMidi, note.midiNote);
    }
    if (firstTick == std::numeric_limits<int64_t>::max()) return;

    const int64_t padding = std::max<int64_t>(std::max<int64_t>(1, module_->editorSnapTick),
                                               (lastTick - firstTick) / 8);
    module_->editorScrollX = static_cast<float>(firstTick - padding);
    module_->editorZoomX = std::clamp((piano_.size.x - 20.f) /
        static_cast<float>(std::max<int64_t>(1, lastTick - firstTick + padding * 2)), 0.02f, 0.8f);

    const int visibleRows = std::max(9, maxMidi - minMidi + 4);
    module_->editorZoomY = std::clamp(piano_.size.y / visibleRows, 11.f, 32.f);
    const float centerMidi = (minMidi + maxMidi) * 0.5f;
    module_->editorScrollY = piano_.size.y * 0.5f - (84.f - centerMidi) * module_->editorZoomY;
}

void VocalEditor::zoomSection() {
    if (module_->score.sections.empty()) return;
    const size_t index = std::min<size_t>(std::lround(module_->params[VocalModule::SECTION_PARAM].getValue()),
                                          module_->score.sections.size() - 1);
    const auto& section = module_->score.sections[index];
    module_->editorScrollX = static_cast<float>(section.startTick);
    module_->editorZoomX = std::clamp((piano_.size.x - 20.f) /
                                          std::max<int64_t>(1, section.endTick - section.startTick),
                                      0.02f, 2.f);
}

void openVocalEditor(VocalModule* module) {
    if (!module) return;
    if (gOpenEditor) {
        APP->event->setSelectedWidget(gOpenEditor);
        return;
    }
    auto* editor = new VocalEditor(module);
    APP->scene->addChild(editor);
    APP->event->setSelectedWidget(editor);
}

}  // namespace vocalrack
