#include "core/Encoding.hpp"
#include "core/ProjectFile.hpp"
#include "core/PitchModel.hpp"
#include "core/Serialization.hpp"
#include "dsp/RealtimeVoiceModulation.hpp"
#include "import/UstxImporter.hpp"
#include "export/UstxExporter.hpp"
#include "phonemizer/Phonemizer.hpp"
#include "render/NativeV1Renderer.hpp"
#include "render/RenderService.hpp"
#include "render/Wav.hpp"
#include "rack/EditorNavigation.hpp"
#include "transport/VocalTransport.hpp"
#include "voicebank/Voicebank.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace vocalrack;
namespace fs = std::filesystem;

struct TestFailure : std::runtime_error { using std::runtime_error::runtime_error; };
#define CHECK(x) do { if (!(x)) throw TestFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK(" #x ") failed"); } while (0)
#define NEAR(a,b,e) CHECK(std::abs((a) - (b)) <= (e))

static void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path()); std::ofstream out(path, std::ios::binary); out.write(text.data(), text.size());
}

static fs::path writeMidiFixture(bool polyphonic = false, bool addMonophonicFallback = false) {
    const auto path = fs::temp_directory_path() /
        ("vocalrack-midi-" + std::to_string(static_cast<long long>(getpid())) + "-" + makeUuid() + ".mid");
    std::vector<unsigned char> bytes;
    const auto raw = [&](std::initializer_list<unsigned char> values) { bytes.insert(bytes.end(), values); };
    const auto be32 = [&](uint32_t value) { raw({static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)}); };
    const auto track = [&](const std::vector<unsigned char>& data) {
        raw({'M','T','r','k'}); be32(static_cast<uint32_t>(data.size())); bytes.insert(bytes.end(), data.begin(), data.end());
    };
    raw({'M','T','h','d'}); be32(6);
    raw({0,1,0,static_cast<unsigned char>(2 + (addMonophonicFallback ? 1 : 0)),1,0xe0});
    track({0,0xff,0x03,9,'C','o','n','d','u','c','t','o','r', 0,0xff,0x51,3,0x07,0xa1,0x20,
           0,0xff,0x58,4,4,2,24,8, 0,0xff,0x2f,0});
    std::vector<unsigned char> melody{0,0xff,0x03,6,'M','e','l','o','d','y',
        0,0xff,0x05,3,0xe3,0x81,0x82, 0,0x90,60,100};
    if (polyphonic) melody.insert(melody.end(), {0,0x90,64,100});
    melody.insert(melody.end(), {0x83,0x60,0x80,60,0});
    if (polyphonic) melody.insert(melody.end(), {0,0x80,64,0});
    melody.insert(melody.end(), {0,0xff,0x05,3,0xe3,0x81,0x84, 0,0x90,64,100,
        0x81,0x70,0x80,64,0, 0x78,0x90,67,100, 0x83,0x60,0x80,67,0, 0,0xff,0x2f,0});
    track(melody);
    if (addMonophonicFallback) {
        track({0,0xff,0x03,5,'V','o','c','a','l',
               0,0xff,0x05,3,0xe3,0x81,0x86, 0,0x90,67,100,
               0x83,0x60,0x80,67,0, 0,0xff,0x2f,0});
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

static fs::path makeTempBank() {
    const auto root = fs::temp_directory_path() /
        ("vocalrack-test-bank-" + std::to_string(static_cast<long long>(getpid())) + "-" + makeUuid());
    fs::create_directories(root / "sub");
    writeText(root / "character.txt", "name=Test Singer\nimage=portrait.png\nauthor=VocalRack\nweb=https://example.invalid\n");
    writeText(root / "portrait.png", "not-an-image-but-present");
    AudioBuffer wave; wave.sampleRate = 44100; wave.samples.resize(22050);
    for (size_t i = 0; i < wave.samples.size(); ++i) wave.samples[i] = 0.35f * std::sin(2.0 * 3.141592653589793 * 220.0 * i / wave.sampleRate);
    writeWavMono16(root / "a.wav", wave); writeWavMono16(root / "i.wav", wave); writeWavMono16(root / "u.wav", wave);
    writeText(root / "oto.ini",
        "a.wav=- あ,0,80,30,40,20\n"
        "a.wav=あ,0,80,30,40,20\n"
        "a.wav=PあS,0,80,30,40,20\n"
        "a.wav=- maI,0,80,30,40,20\n"
        "a.wav=maI,0,80,30,40,20\n"
        "a.wav=aI m,0,80,30,40,20\n"
        "a.wav=aI t,0,80,30,40,20\n"
        "a.wav=tE,0,80,30,40,20\n"
        "a.wav=E st-,0,80,30,40,20\n"
        "a.wav=-te,0,80,30,40,20\n"
        "a.wav=es-,0,80,30,40,20\n"
        "a.wav=st,0,80,30,40,20\n"
        "a.wav=w3,0,80,30,40,20\n"
        "a.wav=3d-,0,80,30,40,20\n"
        "a.wav=dz-,0,80,30,40,20\n"
        "a.wav=- て,0,80,30,40,20\n"
        "a.wav=て,0,80,30,40,20\n"
        "a.wav=e す,0,80,30,40,20\n"
        "a.wav=す,0,80,30,40,20\n"
        "a.wav=u と,0,80,30,40,20\n"
        "a.wav=と,0,80,30,40,20\n"
        "a.wav=か,0,80,30,40,20\n"
        "a.wav=a k,0,80,30,40,20\n");
    writeText(root / "sub" / "oto.ini",
        "../i.wav=a い,0,80,30,40,20\n"
        "../i.wav=い,0,80,30,40,20\n"
        "../i.wav=aI mi,0,80,30,40,20\n"
        "../i.wav=mi,0,80,30,40,20\n"
        "../i.wav=i -,0,80,30,40,20\n"
        "../u.wav=- a,0,80,30,40,20\n"
        "../u.wav=a,0,80,30,40,20\n"
        "../u.wav=う,0,80,30,40,20\n"
        "../u.wav=う,0,80,30,40,20\n"
        "broken line\nmissing.wav=欠,0,80,30,40,20\n");
    writeText(root / "prefix.map", "C4\tP\tS\n");
    return root;
}

static void testEncoding() {
    const std::string cp932("\x91\xab\x97\xa7\x83\x8c\x83\x43", 8);
    TextEncoding detected{}; CHECK(decodeText(cp932, &detected) == "足立レイ"); CHECK(detected == TextEncoding::Cp932);
    CHECK(decodeText("\xef\xbb\xbfhello", &detected) == "hello"); CHECK(detected == TextEncoding::Utf8Bom);
    const std::string utf16le("\xff\xfe\xb3\x8d\xcb\x7a\xec\x30\xa4\x30", 10);
    CHECK(decodeText(utf16le, &detected) == "足立レイ"); CHECK(detected == TextEncoding::Utf16Le);
    bool rejectedOddUtf16 = false;
    try { (void)decodeText(std::string("\xff\xfe\x00", 3)); }
    catch (...) { rejectedOddUtf16 = true; }
    CHECK(rejectedOddUtf16);
    CHECK(isValidUtf8("かな")); CHECK(!isValidUtf8(cp932));
}

static void testEditorNavigation() {
    const auto vertical = editorScrollIntent(0.f, 4.f, false, false);
    NEAR(vertical.pitchPixels, 10.5f, 0.001f);
    NEAR(vertical.timelinePixels, 0.f, 0.001f);

    const auto horizontal = editorScrollIntent(1.5f, 0.f, false, false);
    NEAR(horizontal.timelinePixels, 12.f, 0.001f);
    NEAR(horizontal.pitchPixels, 0.f, 0.001f);

    const auto twoAxis = editorScrollIntent(-0.5f, 0.75f, false, false);
    NEAR(twoAxis.timelinePixels, -4.f, 0.001f);
    NEAR(twoAxis.pitchPixels, 5.25f, 0.001f);

    const auto shiftedWheel = editorScrollIntent(0.f, -2.f, false, true);
    NEAR(shiftedWheel.timelinePixels, -12.f, 0.001f);
    NEAR(shiftedWheel.pitchPixels, 0.f, 0.001f);

    const auto shiftedTrackpad = editorScrollIntent(0.5f, -2.f, false, true);
    NEAR(shiftedTrackpad.timelinePixels, 4.f, 0.001f);
    NEAR(shiftedTrackpad.pitchPixels, 0.f, 0.001f);

    const auto timelineZoom = editorScrollIntent(1.f, 1.f, true, false);
    CHECK(timelineZoom.zoomTimeline); CHECK(!timelineZoom.zoomPitches);
    NEAR(timelineZoom.zoomSteps, 1.f, 0.001f);
    NEAR(timelineZoom.timelinePixels, 0.f, 0.001f);

    const auto pitchZoom = editorScrollIntent(1.f, -1.f, true, true);
    CHECK(pitchZoom.zoomPitches); CHECK(!pitchZoom.zoomTimeline);
    NEAR(pitchZoom.zoomSteps, -1.f, 0.001f);

    CHECK(editorTimelineTailTicks(4, 4) == 15360);  // eight 4/4 bars
    CHECK(editorTimelineTailTicks(3, 8) == 5760);   // eight 3/8 bars
}

static void testMonophonicOverwrite() {
    auto makeNote = [](const std::string& id, int64_t start, int64_t duration) {
        Note note;
        note.id = id;
        note.startTick = start;
        note.durationTick = duration;
        note.pitchCents.points = {{0, 0.f}, {duration / 2, 10.f}, {duration, 20.f}};
        note.dynamicsDb.points = {{0, -6.f}, {duration, 0.f}};
        return note;
    };

    // Drawing/moving a later note across an earlier one truncates the earlier
    // note at the new boundary, matching OpenUtau's pencil/fix-overlap result.
    VocalScore tailCollision;
    tailCollision.notes = {makeNote("old", 0, 480), makeNote("edited", 240, 480)};
    resolveMonophonicOverwrite(tailCollision, {"edited"});
    CHECK(tailCollision.validate().empty());
    CHECK(tailCollision.notes.size() == 2);
    CHECK(tailCollision.notes[0].id == "old");
    CHECK(tailCollision.notes[0].durationTick == 240);
    CHECK(tailCollision.notes[0].pitchCents.points.back().tickOffset == 240);
    NEAR(tailCollision.notes[0].pitchCents.points.back().value, 10.f, 0.001f);
    CHECK(tailCollision.notes[1].id == "edited");
    CHECK(tailCollision.notes[1].startTick == 240);
    CHECK(tailCollision.notes[1].durationTick == 480);

    // If the edited note erases the head of its neighbour, retain and rebase
    // the neighbour's surviving suffix instead of deleting useful material.
    VocalScore headCollision;
    headCollision.notes = {makeNote("edited", 0, 360), makeNote("old", 240, 480)};
    resolveMonophonicOverwrite(headCollision, {"edited"});
    CHECK(headCollision.validate().empty());
    CHECK(headCollision.notes.size() == 2);
    const auto old = std::find_if(headCollision.notes.begin(), headCollision.notes.end(),
        [](const Note& note) { return note.id == "old"; });
    CHECK(old != headCollision.notes.end());
    CHECK(old->startTick == 360);
    CHECK(old->durationTick == 360);
    CHECK(old->pitchCents.points.front().tickOffset == 0);
    NEAR(old->pitchCents.points.front().value, 5.f, 0.001f);

    // A note spanning the overwrite keeps only its earliest remaining span,
    // rather than cloning its lyric into a surprise right-hand fragment.
    VocalScore middleCollision;
    middleCollision.notes = {makeNote("old", 0, 960), makeNote("edited", 240, 240)};
    resolveMonophonicOverwrite(middleCollision, {"edited"});
    CHECK(middleCollision.validate().empty());
    CHECK(middleCollision.notes.size() == 2);
    CHECK(middleCollision.notes[0].id == "old");
    CHECK(middleCollision.notes[0].durationTick == 240);

    // Full coverage removes the old note. Multiple edited notes remain a
    // rigid group and jointly overwrite every unselected collision.
    VocalScore groupCollision;
    groupCollision.notes = {
        makeNote("covered", 120, 120), makeNote("left", 0, 240),
        makeNote("right", 240, 240), makeNote("after", 420, 240),
    };
    resolveMonophonicOverwrite(groupCollision, {"left", "right"});
    CHECK(groupCollision.validate().empty());
    CHECK(groupCollision.notes.size() == 3);
    CHECK(std::none_of(groupCollision.notes.begin(), groupCollision.notes.end(),
        [](const Note& note) { return note.id == "covered"; }));
    const auto after = std::find_if(groupCollision.notes.begin(), groupCollision.notes.end(),
        [](const Note& note) { return note.id == "after"; });
    CHECK(after != groupCollision.notes.end());
    CHECK(after->startTick == 480);
    CHECK(after->durationTick == 180);

    // Notes outside the overwritten time keep their authored pre/post-roll
    // pitch points byte-for-byte; resolving one collision must not rewrite
    // unrelated expression data elsewhere in a song.
    VocalScore unrelatedCurves;
    auto far = makeNote("far", 960, 480);
    far.pitchCents.points = {{-60, -5.f}, {540, 30.f}};
    unrelatedCurves.notes = {makeNote("edited", 0, 480), far};
    resolveMonophonicOverwrite(unrelatedCurves, {"edited"});
    const auto untouched = std::find_if(unrelatedCurves.notes.begin(), unrelatedCurves.notes.end(),
        [](const Note& note) { return note.id == "far"; });
    CHECK(untouched != unrelatedCurves.notes.end());
    CHECK(untouched->pitchCents.points.size() == 2);
    CHECK(untouched->pitchCents.points[0].tickOffset == -60);
    CHECK(untouched->pitchCents.points[1].tickOffset == 540);

    // These are the exact shared operations called by the Rack mouse widget,
    // not a second test-only approximation of its gesture math.
    VocalScore drawn;
    drawn.notes = {makeNote("old", 0, 480)};
    placeEditorDrawnNote(drawn, makeNote("drawn", 240, 480));
    CHECK(drawn.validate().empty());
    CHECK(drawn.notes.size() == 2);
    CHECK(drawn.notes[0].id == "old");
    CHECK(drawn.notes[0].durationTick == 240);
    CHECK(drawn.notes[1].id == "drawn");

    VocalScore moved;
    moved.notes = {makeNote("old", 0, 480), makeNote("moving", 480, 240)};
    applyEditorNoteGesture(moved, {
        EditorNoteGestureKind::Move, {"moving"}, "moving", -190, 2, true, 120,
    });
    CHECK(moved.validate().empty());
    CHECK(moved.notes[0].id == "old");
    CHECK(moved.notes[0].durationTick == 240);
    CHECK(moved.notes[1].id == "moving");
    CHECK(moved.notes[1].startTick == 240);
    CHECK(moved.notes[1].midiNote == 62);

    VocalScore resizedEnd;
    resizedEnd.notes = {makeNote("edited", 0, 240), makeNote("old", 360, 360)};
    applyEditorNoteGesture(resizedEnd, {
        EditorNoteGestureKind::ResizeEnd, {"edited"}, "edited", 250, 0, true, 120,
    });
    CHECK(resizedEnd.validate().empty());
    CHECK(resizedEnd.notes[0].durationTick == 480);
    CHECK(resizedEnd.notes[1].startTick == 480);
    CHECK(resizedEnd.notes[1].durationTick == 240);

    VocalScore resizedStart;
    resizedStart.notes = {makeNote("old", 0, 360), makeNote("edited", 480, 240)};
    applyEditorNoteGesture(resizedStart, {
        EditorNoteGestureKind::ResizeStart, {"edited"}, "edited", -250, 0, true, 120,
    });
    CHECK(resizedStart.validate().empty());
    CHECK(resizedStart.notes[0].id == "old");
    CHECK(resizedStart.notes[0].durationTick == 240);
    CHECK(resizedStart.notes[1].id == "edited");
    CHECK(resizedStart.notes[1].startTick == 240);
    CHECK(resizedStart.notes[1].durationTick == 480);

    VocalScore shortImported;
    shortImported.notes = {makeNote("short", 480, 60)};
    applyEditorNoteGesture(shortImported, {
        EditorNoteGestureKind::ResizeStart, {"short"}, "short", 240, 0, true, 120,
    });
    CHECK(shortImported.validate().empty());
    CHECK(shortImported.notes[0].startTick == 480);
    CHECK(shortImported.notes[0].durationTick == 60);

    VocalScore movedGroup;
    movedGroup.notes = {
        makeNote("obstacle", 0, 360), makeNote("left", 480, 120), makeNote("right", 600, 120),
    };
    applyEditorNoteGesture(movedGroup, {
        EditorNoteGestureKind::Move, {"left", "right"}, "left", -240, -1, true, 120,
    });
    CHECK(movedGroup.validate().empty());
    CHECK(movedGroup.notes.size() == 3);
    CHECK(movedGroup.notes[0].id == "obstacle");
    CHECK(movedGroup.notes[0].durationTick == 240);
    CHECK(movedGroup.notes[1].id == "left");
    CHECK(movedGroup.notes[1].startTick == 240);
    CHECK(movedGroup.notes[2].id == "right");
    CHECK(movedGroup.notes[2].startTick == 360);
}

static void testVoicebankAndPhonemizers() {
    const auto root = makeTempBank();
    auto bank = Voicebank::load(root, "test-bank");
    CHECK(bank.valid()); CHECK(bank.character.name == "Test Singer"); CHECK(bank.character.author == "VocalRack");
    CHECK(bank.entries.size() == 32); CHECK(bank.findAlias("あ", 59) != nullptr); CHECK(bank.findAlias("あ", 60) != nullptr);
    CHECK(bank.findAlias("い", 60) != nullptr); CHECK(!bank.diagnostics.duplicateAliases.empty());
    CHECK(bank.diagnostics.warnings.size() >= 3);  // duplicate, malformed line, and missing WAV all fail safely.
    CHECK(toneNameToMidi("C4") == 60); CHECK(midiToToneName(60) == "C4");
    CHECK(normalizeJapanese("アダチレイ") == "あだちれい");
    CHECK(normalizeJapanese("da") == "だ"); CHECK(normalizeJapanese("adachirei") == "あだちれい");
    CHECK(normalizeJapanese("kya") == "きゃ"); CHECK(normalizeJapanese("gakkou") == "がっこう");
    CHECK(validJapaneseLyricInput("da")); CHECK(validJapaneseLyricInput("足立レイ"));
    CHECK(validJapaneseLyricInput("+1")); CHECK(validJapaneseLyricInput("-"));
    CHECK(!validJapaneseLyricInput("")); CHECK(!validJapaneseLyricInput("notajapaneselyric"));
    CHECK(trailingVowel("きゃ") == "a"); CHECK(trailingVowel("da") == "a"); CHECK(trailingVowel("ん") == "n");

    Note first; first.id = makeUuid(); first.startTick = 0; first.durationTick = 480; first.midiNote = 59; first.lyric = "あ";
    Note second; second.id = makeUuid(); second.startTick = 480; second.durationTick = 480; second.midiNote = 60; second.lyric = "い";
    JapaneseAutoPhonemizer automatic;
    auto start = automatic.process(first, nullptr, &second, bank); CHECK(start.oto); CHECK(start.selectedAlias == "- あ");
    Note romaji = first; romaji.id = makeUuid(); romaji.lyric = "a";
    auto romanized = automatic.process(romaji, nullptr, &second, bank); CHECK(romanized.oto); CHECK(romanized.selectedAlias == "- あ");
    auto vcv = automatic.process(second, &first, nullptr, bank); CHECK(vcv.oto); CHECK(vcv.selectedAlias == "a い");
    Note tied = second; tied.id = makeUuid(); tied.startTick = 960; tied.lyric = "-"; tied.aliasOverride.reset();
    auto tie = automatic.process(tied, &second, nullptr, bank); CHECK(tie.oto); CHECK(tie.selectedAlias == "い");
    tied.lyric = "+"; auto plusTie = automatic.process(tied, &second, nullptr, bank); CHECK(plusTie.oto); CHECK(plusTie.selectedAlias == "い");
    CHECK(lyricIsExtender("ー")); CHECK(sustainedVowelKana(&second) == "い");
    // Prove production English lookup uses the packaged dictionary. The small
    // singing-synth lexicon does not contain this exact pronunciation, and the
    // spelling fallback produces a different result.
    setEnglishDictionaryPath("res/dictionaries/cmudict-0.7b.txt");
    CHECK(englishToXSampa("synthesizer") ==
          std::vector<std::string>({"s", "I", "n", "T", "V", "s", "aI", "z", "3"}));
    CHECK(englishToXSampa("openutau") ==
          std::vector<std::string>({"oU", "p", "E", "n", "V", "t", "aU"}));
    CHECK(englishToXSampa("utau") ==
          std::vector<std::string>({"j", "u", "t", "oU"}));
    CHECK(englishToXSampa("my") == std::vector<std::string>({"m", "aI"}));
    CHECK(englishToXSampa("read [r i d]") == std::vector<std::string>({"r", "i", "d"}));
    CHECK(trailingXSampaVowel("voice") == "OI");
    EnglishXSampaPhonemizer english;
    Note englishFirst = first; englishFirst.lyric = "my";
    Note englishSecond = second; englishSecond.lyric = "me";
    auto englishStart = english.processAll(englishFirst, nullptr, &englishSecond, bank);
    CHECK(englishStart.size() == 1); CHECK(englishStart[0].oto); CHECK(englishStart[0].selectedAlias == "- maI");
    auto englishContext = english.processAll(englishSecond, &englishFirst, nullptr, bank);
    CHECK(englishContext.size() == 2);
    CHECK(englishContext[0].selectedAlias == "aI mi");
    CHECK(englishContext[1].selectedAlias == "i -");
    CHECK(englishContext[0].relativeTick == 480);
    CHECK(englishContext[1].relativeTick == 900);
    Note englishTest = second; englishTest.id = makeUuid(); englishTest.lyric = "test";
    auto xsampaPhrase = english.processAll(englishTest, &englishFirst, nullptr, bank);
    CHECK(xsampaPhrase.size() == 3);
    CHECK(xsampaPhrase[0].selectedAlias == "aI t");
    CHECK(xsampaPhrase[1].selectedAlias == "tE");
    CHECK(xsampaPhrase[2].selectedAlias == "E st-");
    CHECK(xsampaPhrase[0].relativeTick == 375);
    CHECK(xsampaPhrase[1].relativeTick == 480);
    CHECK(xsampaPhrase[2].relativeTick == 900);

    EnglishVccvPhonemizer vccv;
    Note vccvFirst = first; vccvFirst.lyric = "test";
    Note vccvSecond = second; vccvSecond.lyric = "words";
    const auto vccvA = vccv.processAll(vccvFirst, nullptr, &vccvSecond, bank);
    const auto vccvB = vccv.processAll(vccvSecond, &vccvFirst, nullptr, bank);
    CHECK(vccvA.size() == 3); CHECK(vccvB.size() == 3);
    CHECK(vccvA[0].selectedAlias == "-te"); CHECK(vccvA[1].selectedAlias == "es-");
    CHECK(vccvA[2].selectedAlias == "st"); CHECK(vccvB[0].selectedAlias == "w3");
    CHECK(vccvB[1].selectedAlias == "3d-"); CHECK(vccvB[2].selectedAlias == "dz-");
    CHECK(vccvA[0].relativeTick == 0); CHECK(vccvA[1].relativeTick == 370);
    CHECK(vccvA[2].relativeTick == 425); CHECK(vccvB[0].relativeTick == 480);
    CHECK(vccvB[1].relativeTick == 790); CHECK(vccvB[2].relativeTick == 845);

    EnglishToJapanesePhonemizer englishToJapanese;
    Note japaneseEnglish = first; japaneseEnglish.lyric = "test"; japaneseEnglish.durationTick = 960;
    Note adjacentEnglish = second; adjacentEnglish.startTick = 960; adjacentEnglish.lyric = "words";
    const auto accented = englishToJapanese.processAll(japaneseEnglish, nullptr, &adjacentEnglish, bank);
    CHECK(accented.size() == 1); CHECK(accented[0].selectedAlias == "- て");
    CHECK(accented[0].relativeTick == 0);
    const auto connectedWords = englishToJapanese.processAll(
        adjacentEnglish, &japaneseEnglish, nullptr, bank);
    CHECK(connectedWords.size() == 5);
    CHECK(connectedWords[0].requestedAlias == "す");
    CHECK(connectedWords[1].requestedAlias == "と");
    CHECK(connectedWords[2].requestedAlias == "うぉ");
    CHECK(connectedWords[3].requestedAlias == "ど");
    CHECK(connectedWords[4].requestedAlias == "ず");
    CHECK(connectedWords[0].relativeTick == 850);
    CHECK(connectedWords[1].relativeTick == 905);
    CHECK(connectedWords[2].relativeTick == 960);

    // Regression for OpenUtau's cross-word consonant transfer. It is not
    // enough that both words are non-silent: /n/ must become the onset of the
    // next vowel, making "an up" え・な・ぷ rather than あ・ん・あ・ぷ.
    Note an = japaneseEnglish; an.lyric = "an";
    Note up = adjacentEnglish; up.lyric = "up";
    const auto anEvents = englishToJapanese.processAll(an, nullptr, &up, bank);
    const auto upEvents = englishToJapanese.processAll(up, &an, nullptr, bank);
    CHECK(anEvents.size() == 1);
    CHECK(anEvents[0].requestedAlias == "え");
    CHECK(upEvents.size() == 2);
    CHECK(upEvents[0].requestedAlias == "な");
    CHECK(upEvents[1].requestedAlias == "ぷ");

    // Connected duplicate consonants and special clusters follow OpenUtau's
    // SyllableBasedPhonemizer collapse rules. Without this, joins inserted an
    // audible extra と/い/ん and shifted the intended onset earlier.
    Note priorTest = japaneseEnglish; priorTest.startTick = 0; priorTest.durationTick = 960;
    Note nextTest = priorTest; nextTest.id = makeUuid(); nextTest.startTick = 960;
    const auto repeatedTest = englishToJapanese.processAll(nextTest, &priorTest, nullptr, bank);
    CHECK(repeatedTest.size() >= 2);
    CHECK(repeatedTest[0].requestedAlias == "す");
    CHECK(repeatedTest[1].requestedAlias == "て");
    CHECK(repeatedTest[0].relativeTick == 905);
    CHECK(repeatedTest[1].relativeTick == 960);

    Note priorMy = priorTest; priorMy.lyric = "my";
    Note nextYou = nextTest; nextYou.lyric = "you";
    const auto myToYou = englishToJapanese.processAll(nextYou, &priorMy, nullptr, bank);
    CHECK(!myToYou.empty());
    CHECK(myToYou.front().requestedAlias == "ゆ");

    Note priorStreet = priorTest; priorStreet.lyric = "street";
    Note nextSpring = nextTest; nextSpring.lyric = "spring";
    const auto streetToSpring = englishToJapanese.processAll(nextSpring, &priorStreet, nullptr, bank);
    CHECK(streetToSpring.size() >= 3);
    CHECK(streetToSpring[0].requestedAlias == "つ");
    CHECK(streetToSpring[1].requestedAlias == "ぷ");

    Note openUtauWord = priorTest; openUtauWord.lyric = "openutau";
    Note utauWord = nextTest; utauWord.lyric = "utau";
    const auto productTiming = englishToJapanese.processAll(
        openUtauWord, nullptr, &utauWord, bank);
    CHECK(productTiming.size() == 5);
    CHECK(productTiming[0].requestedAlias == "お");
    CHECK(productTiming[0].relativeTick == 0);
    CHECK(productTiming[1].requestedAlias == "う");
    CHECK(productTiming[1].relativeTick == 170);
    CHECK(productTiming[2].requestedAlias == "ぺ");
    CHECK(productTiming[2].relativeTick == 225);
    CHECK(productTiming[3].requestedAlias == "な");
    CHECK(productTiming[3].relativeTick == 450);
    CHECK(productTiming[4].requestedAlias == "た");
    CHECK(productTiming[4].relativeTick == 675);
    Note leadIn; leadIn.id = makeUuid(); leadIn.startTick = 0; leadIn.durationTick = 960;
    leadIn.midiNote = 55; leadIn.lyric = "a";
    Note star = leadIn; star.id = makeUuid(); star.startTick = 960; star.lyric = "star";
    Note afterRest = star; afterRest.id = makeUuid(); afterRest.startTick = 2160; afterRest.lyric = "we";
    const auto starLayout = englishToJapanese.processAll(star, &leadIn, &afterRest, bank);
    CHECK(starLayout.size() == 3);
    CHECK(starLayout[0].requestedAlias == "す"); CHECK(starLayout[0].relativeTick == 905);
    CHECK(starLayout[1].requestedAlias == "た"); CHECK(starLayout[1].relativeTick == 960);
    CHECK(starLayout[2].requestedAlias == "う"); CHECK(starLayout[2].relativeTick == 1805);

    JapaneseCvvcPhonemizer cvvc;
    Note cvvcFirst = first; cvvcFirst.lyric = "あ";
    Note cvvcSecond = second; cvvcSecond.lyric = "か";
    const auto cvvcEvents = cvvc.processAll(cvvcFirst, nullptr, &cvvcSecond, bank);
    CHECK(cvvcEvents.size() == 2); CHECK(cvvcEvents[0].selectedAlias == "- あ");
    CHECK(cvvcEvents[1].selectedAlias == "a k");
    CHECK(makePhonemizer(kEnglishXSampaPhonemizer)->name() == kEnglishXSampaPhonemizer);
    CHECK(makePhonemizer(kEnglishVccvPhonemizer)->name() == kEnglishVccvPhonemizer);
    CHECK(makePhonemizer(kJapaneseCvvcPhonemizer)->name() == kJapaneseCvvcPhonemizer);
    CHECK(makePhonemizer(kEnglishToJapanesePhonemizer)->name() == kEnglishToJapanesePhonemizer);
    CHECK(makePhonemizer(kLegacyEnglishToJapanesePhonemizer)->name() == kEnglishToJapanesePhonemizer);
    CHECK(makePhonemizer("unknown future name")->name() == kEnglishXSampaPhonemizer);
    DirectAliasPhonemizer direct; second.aliasOverride = "う";
    auto escaped = direct.process(second, &first, nullptr, bank); CHECK(escaped.oto); CHECK(escaped.selectedAlias == "う");

    VocalScore englishScore;
    englishFirst.startTick = 0; englishFirst.durationTick = 480; englishFirst.midiNote = 57;
    englishSecond.startTick = 480; englishSecond.durationTick = 480; englishSecond.midiNote = 60;
    englishScore.notes = {englishFirst, englishSecond}; englishScore.normalize();
    RenderOptions englishOptions; englishOptions.phonemizer = kEnglishXSampaPhonemizer;
    const auto englishAudio = NativeV1Renderer{}.render(englishScore, bank, englishOptions);
    CHECK(finiteAudio(englishAudio)); CHECK(peakAudio(englishAudio) > 0.01f);
    CHECK(englishAudio.diagnostics.errors.empty());
    CHECK(englishAudio.diagnostics.phonemes.size() == 3);
    fs::remove_all(root);
}

static void testScoreSerialization() {
    auto original = makeDefaultScore();
    std::ifstream templateInput("tests/fixtures/english_first_sound.json", std::ios::binary);
    CHECK(templateInput.good());
    const std::string templateJson((std::istreambuf_iterator<char>(templateInput)),
                                   std::istreambuf_iterator<char>());
    const auto templateFixture = scoreFromJson(templateJson);
    CHECK(templateFixture.title == original.title);
    CHECK(templateFixture.notes.size() == original.notes.size());
    CHECK(templateFixture.sections.size() == original.sections.size());
    for (size_t index = 0; index < original.notes.size(); ++index) {
        const auto& expected = original.notes[index];
        const auto& fixture = templateFixture.notes[index];
        CHECK(fixture.startTick == expected.startTick);
        CHECK(fixture.durationTick == expected.durationTick);
        CHECK(fixture.midiNote == expected.midiNote);
        CHECK(fixture.lyric == expected.lyric);
        CHECK(fixture.pitchCents.points.size() == expected.pitchCents.points.size());
        for (size_t point = 0; point < expected.pitchCents.points.size(); ++point) {
            CHECK(fixture.pitchCents.points[point].tickOffset ==
                  expected.pitchCents.points[point].tickOffset);
            NEAR(fixture.pitchCents.points[point].value,
                 expected.pitchCents.points[point].value, 0.001f);
        }
        CHECK(fixture.dynamicsDb.points.size() == expected.dynamicsDb.points.size());
        for (size_t point = 0; point < expected.dynamicsDb.points.size(); ++point) {
            CHECK(fixture.dynamicsDb.points[point].tickOffset ==
                  expected.dynamicsDb.points[point].tickOffset);
            NEAR(fixture.dynamicsDb.points[point].value,
                 expected.dynamicsDb.points[point].value, 0.001f);
        }
        NEAR(fixture.vibrato.startPercent, expected.vibrato.startPercent, 0.001f);
        NEAR(fixture.vibrato.depthCents, expected.vibrato.depthCents, 0.001f);
        NEAR(fixture.vibrato.rateHz, expected.vibrato.rateHz, 0.001f);
        NEAR(fixture.vibrato.fadeInPercent, expected.vibrato.fadeInPercent, 0.001f);
        NEAR(fixture.vibrato.fadeOutPercent, expected.vibrato.fadeOutPercent, 0.001f);
    }
    for (size_t index = 0; index < original.sections.size(); ++index) {
        CHECK(templateFixture.sections[index].name == original.sections[index].name);
        CHECK(templateFixture.sections[index].startTick == original.sections[index].startTick);
        CHECK(templateFixture.sections[index].endTick == original.sections[index].endTick);
    }
    original.notes[0].phonemeTiming.positionOffsetTick = -12;
    original.notes[0].phonemeTiming.internalPositionOffsetTicks = {18, -9};
    original.notes[0].pitchSnapFirst = false;
    original.notes[0].phonemeTiming.preutteranceDeltaMs = 8.f;
    original.notes[0].phonemeTiming.overlapDeltaMs = -3.f;
    const auto encoded = scoreToJson(original, true); auto decoded = scoreFromJson(encoded);
    CHECK(decoded.notes.size() == original.notes.size()); CHECK(decoded.sections.size() == 2);
    CHECK(decoded.title == original.title);
    CHECK(!decoded.notes[0].pitchSnapFirst); CHECK(decoded.notes[1].pitchSnapFirst);
    NEAR(decoded.notes[2].pitchCents.sample(480), 20.f, 0.001f);
    CHECK(decoded.notes[0].phonemeTiming.positionOffsetTick == -12);
    CHECK(decoded.notes[0].phonemeTiming.internalPositionOffsetTicks ==
          std::vector<int64_t>({18, -9}));
    NEAR(*decoded.notes[0].phonemeTiming.preutteranceDeltaMs, 8.f, 0.001f);
    NEAR(*decoded.notes[0].phonemeTiming.overlapDeltaMs, -3.f, 0.001f);
    Note boundaryNote;
    boundaryNote.startTick = 0;
    boundaryNote.durationTick = 480;
    const std::vector<int64_t> automaticTicks{0, 120, 300, 420};
    CHECK(setInternalPhonemeBoundaryTick(boundaryNote, automaticTicks, 1, 200));
    CHECK(adjustedInternalPhonemeTick(boundaryNote, 1, automaticTicks[1]) == 200);
    CHECK(adjustedInternalPhonemeTick(boundaryNote, 0, automaticTicks[0]) == 0);
    CHECK(setInternalPhonemeBoundaryTick(boundaryNote, automaticTicks, 2, 190));
    CHECK(adjustedInternalPhonemeTick(boundaryNote, 2, automaticTicks[2]) == 201);
    CHECK(setInternalPhonemeBoundaryTick(boundaryNote, automaticTicks, 3, 900));
    CHECK(adjustedInternalPhonemeTick(boundaryNote, 3, automaticTicks[3]) == 479);
    CHECK(!setInternalPhonemeBoundaryTick(boundaryNote, automaticTicks, 0, 60));
    std::string migration;
    auto old = scoreFromJson(R"({"schemaVersion":1,"title":"old","bpm":90,"notes":[{"position":0,"duration":480,"tone":60,"lyric":"あ"}]})", &migration);
    CHECK(old.nominalBpm == 90); CHECK(!migration.empty()); CHECK(old.schemaVersion == 2);
    auto overlap = original; overlap.notes[1].startTick = 100; overlap.normalize(); CHECK(!overlap.validate().empty());
    auto sectionOverlap = original; sectionOverlap.sections[1].startTick = 100; sectionOverlap.normalize(); CHECK(!sectionOverlap.validate().empty());
    const auto drone = makeDroneScore(); CHECK(drone.notes.size() == 1); CHECK(drone.notes[0].lyric == "う");
    CHECK(drone.sections.size() == 1); CHECK(drone.validate().empty());
    const auto word = makeTriggeredWordScore(); CHECK(word.notes.size() == 3); CHECK(word.endTick() == 960); CHECK(word.validate().empty());
    const auto loopPhrase = makeLoopPhraseScore(); CHECK(loopPhrase.notes.size() == 4); CHECK(loopPhrase.sections.size() == 1);
    CHECK(loopPhrase.sections[0].endTick - loopPhrase.notes.back().endTick() == kTicksPerQuarter); CHECK(loopPhrase.validate().empty());
    const auto englishDrone = makeEnglishDroneScore(); CHECK(englishDrone.notes.size() == 1); CHECK(englishDrone.notes[0].lyric == "a");
    const auto englishWord = makeEnglishTriggeredWordScore(); CHECK(englishWord.notes.size() == 1); CHECK(englishWord.notes[0].lyric == "sing");
    const auto englishLoop = makeEnglishLoopPhraseScore(); CHECK(englishLoop.notes.size() == 4); CHECK(englishLoop.sections.size() == 1);
    CHECK(englishLoop.sections[0].endTick - englishLoop.notes.back().endTick() == kTicksPerQuarter); CHECK(englishLoop.validate().empty());

    VocalScore connected;
    Note low; low.id = makeUuid(); low.startTick = 0; low.durationTick = 480; low.midiNote = 60;
    Note high; high.id = makeUuid(); high.startTick = 480; high.durationTick = 480; high.midiNote = 64;
    connected.notes = {low, high}; connected.normalize();
    const int64_t half = defaultPortamentoHalfTicks(120.0);
    NEAR(implicitPortamentoCents(connected, 1, -half, 120.0), -400.f, 0.001f);
    NEAR(implicitPortamentoCents(connected, 1, 0, 120.0), -200.f, 0.001f);
    NEAR(implicitPortamentoCents(connected, 1, half, 120.0), 0.f, 0.001f);
    connected.notes[1].startTick += 120;
    NEAR(implicitPortamentoCents(connected, 1, 0, 120.0), 0.f, 0.001f);
    connected.notes[1].startTick -= 120;
    const int64_t boundary = connected.notes[1].startTick;
    NEAR(performedAbsoluteMidi(connected, boundary - half, 120.0), 60.f, 0.001f);
    NEAR(performedAbsoluteMidi(connected, boundary, 120.0), 62.f, 0.001f);
    NEAR(performedAbsoluteMidi(connected, boundary + half, 120.0), 64.f, 0.001f);
    connected.notes[1].pitchCents.points = {{0, -15.f}, {240, 20.f}, {480, 0.f}};
    NEAR(performedAbsoluteMidi(connected, boundary, 120.0), 60.f, 0.001f);
    connected.notes[1].pitchSnapFirst = false;
    NEAR(performedAbsoluteMidi(connected, boundary, 120.0), 63.85f, 0.001f);
}

static void testUstx() {
    UstxImporter importer;
    auto tracks = importer.scanTracks("tests/fixtures/multiple_tracks.ustx");
    CHECK(tracks.size() == 2); CHECK(tracks[0].name == "Lead"); CHECK(tracks[1].name == "Harmony");
    auto harmony = importer.importTrack("tests/fixtures/multiple_tracks.ustx", 1); CHECK(harmony.score.notes.size() == 1); CHECK(harmony.score.notes[0].midiNote == 67);
    auto simple = importer.importTrack("tests/fixtures/simple_japanese.ustx", 0);
    CHECK(simple.score.notes.size() == 2); CHECK(!simple.score.notes[0].pitchCents.points.empty());
    CHECK(simple.score.notes[0].phonemeTiming.positionOffsetTick == -12);
    NEAR(*simple.score.notes[0].phonemeTiming.preutteranceDeltaMs, 8.f, 0.001f);
    NEAR(*simple.score.notes[0].phonemeTiming.overlapDeltaMs, -3.f, 0.001f);
    NEAR(*simple.score.notes[0].phonemeTiming.attackTimeDeltaMs, 2.f, 0.001f);
    NEAR(*simple.score.notes[0].phonemeTiming.releaseTimeDeltaMs, 4.f, 0.001f);
    CHECK(!simple.score.notes[0].dynamicsDb.points.empty()); CHECK(simple.score.notes[1].vibrato.depthCents == 30);
    NEAR(simple.score.notes[1].vibrato.phase, 3.141592653589793 / 2.0, 0.001);
    NEAR(simple.score.notes[0].dynamicsDb.sample(240), -1.f, 0.001f);
    NEAR(simple.score.notes[1].dynamicsDb.sample(240), 0.5f, 0.001f);
    NEAR(simple.score.notes[1].dynamicsDb.sample(720), 0.f, 0.001f);
    CHECK(simple.report.ignored.size() >= 4);
    auto legacyTracks = importer.scanTracks("tests/fixtures/legacy_song.ust");
    CHECK(legacyTracks.size() == 1); CHECK(legacyTracks[0].noteCount == 16);
    auto legacy = importer.importTrack("tests/fixtures/legacy_song.ust", 0);
    CHECK(legacy.score.notes.size() == 16); CHECK(legacy.score.notes[0].aliasOverride == "あ");
    CHECK(legacy.score.notes[5].lyric == "+"); CHECK(!legacy.score.notes[5].aliasOverride);
    CHECK(legacy.score.notes[6].startTick == 2640); CHECK(legacy.score.notes[11].lyric == "-");
    CHECK(!legacy.report.approximated.empty()); CHECK(!legacy.report.ignored.empty());
    const auto midiPath = writeMidiFixture();
    auto midiTracks = importer.scanTracks(midiPath); CHECK(midiTracks.size() == 1); CHECK(midiTracks[0].name == "Melody");
    auto midi = importer.importTrack(midiPath, 0); CHECK(midi.score.notes.size() == 3);
    CHECK(midi.score.notes[0].lyric == "あ"); CHECK(midi.score.notes[1].lyric == "い"); CHECK(midi.score.notes[2].lyric == "a");
    CHECK(midi.score.notes[2].startTick == 840); CHECK(midi.score.notes[2].midiNote == 67); CHECK(!midi.report.warnings.empty());
    fs::remove(midiPath);
    const auto polyPath = writeMidiFixture(true); bool polyThrew = false;
    try { importer.importTrack(polyPath, 0); } catch (...) { polyThrew = true; }
    fs::remove(polyPath); CHECK(polyThrew);
    const auto mixedMidiPath = writeMidiFixture(true, true);
    auto mixedTracks = importer.scanTracks(mixedMidiPath); CHECK(mixedTracks.size() == 1); CHECK(mixedTracks[0].name == "Vocal");
    auto mixedMidi = importer.importTrack(mixedMidiPath, 0); CHECK(mixedMidi.score.notes.size() == 1);
    CHECK(mixedMidi.score.notes[0].lyric == "う"); CHECK(!mixedMidi.report.warnings.empty());
    fs::remove(mixedMidiPath);
    auto warnings = importer.importTrack("tests/fixtures/tempo_map_warning.ustx", 0);
    CHECK(warnings.score.nominalBpm == 120); CHECK(warnings.score.beatsPerBar == 4); CHECK(warnings.report.warnings.size() == 2); CHECK(!warnings.report.ignored.empty());
    auto comparison = importer.importTrack("tests/fixtures/adachi_rei_ab.ustx", 0);
    CHECK(comparison.score.notes.size() == 6); CHECK(comparison.score.notes[0].midiNote == 55);
    CHECK(comparison.score.notes[5].startTick == 2880); CHECK(comparison.score.notes[5].lyric == "う");
    CHECK(comparison.score.notes[2].pitchCents.points.size() > 2);
    CHECK(comparison.score.notes[2].pitchSnapFirst);
    NEAR(comparison.score.notes[2].pitchCents.points.front().value, -200.f, 0.01f);
    NEAR(comparison.score.notes[2].pitchCents.sample(0), -100.f, 1.f);
    NEAR(comparison.score.notes[4].vibrato.depthCents, 0.f, 0.01f);
    bool threw = false; try { importer.importTrack("tests/fixtures/malformed.ustx", 0); } catch (...) { threw = true; } CHECK(threw);

    const auto hostileOverride = fs::temp_directory_path() / ("hostile-override-" + makeUuid() + ".ustx");
    writeText(hostileOverride,
        "name: hostile\nustx_version: 0.9\ntempos:\n- {position: 0, bpm: 120}\n"
        "tracks:\n- track_name: Lead\nvoice_parts:\n- name: Main\n  track_no: 0\n  position: 0\n"
        "  notes:\n  - position: 0\n    duration: 480\n    tone: 60\n    lyric: a\n"
        "    phoneme_overrides:\n    - {index: 2147483647, phoneme: a}\n");
    threw = false; try { importer.scanTracks(hostileOverride); } catch (...) { threw = true; }
    fs::remove(hostileOverride); CHECK(threw);

    const auto hostileCurve = fs::temp_directory_path() / ("hostile-curve-" + makeUuid() + ".ustx");
    writeText(hostileCurve,
        "name: hostile\nustx_version: 0.9\ntempos:\n- {position: 0, bpm: 120}\n"
        "tracks:\n- track_name: Lead\nvoice_parts:\n- name: Main\n  track_no: 0\n  position: 0\n"
        "  notes:\n  - position: 0\n    duration: 480\n    tone: 60\n    lyric: a\n"
        "    pitch:\n      data:\n      - {x: 0, y: 0, shape: l}\n"
        "      - {x: 1000000000000, y: 0, shape: l}\n");
    threw = false; try { importer.scanTracks(hostileCurve); } catch (...) { threw = true; }
    fs::remove(hostileCurve); CHECK(threw);
}

static void testProjectAndUstxRoundTrip() {
    VocalProjectState original;
    original.score.title = "Round-trip 足立レイ";
    original.score.nominalBpm = 137.5;
    original.score.beatsPerBar = 3;
    Note first;
    first.id = makeUuid(); first.startTick = 0; first.durationTick = 720;
    first.midiNote = 62; first.lyric = "だ";
    first.phonemeOverrides = {"a だ", "ち", "i れ"};
    first.pitchSnapFirst = false;
    first.pitchCents.points = {{0, -20.f}, {240, 35.f}, {720, 0.f}};
    first.dynamicsDb.points = {{0, -3.f}, {360, 2.f}, {720, -1.f}};
    first.vibrato.startPercent = 55.f; first.vibrato.depthCents = 31.f;
    first.vibrato.rateHz = 6.25f; first.vibrato.fadeInPercent = 12.f;
    first.vibrato.fadeOutPercent = 18.f; first.vibrato.phase = 1.2f;
    first.phonemeTiming.positionOffsetTick = -14;
    first.phonemeTiming.internalPositionOffsetTicks = {24, -11};
    first.phonemeTiming.preutteranceDeltaMs = 9.5f;
    first.phonemeTiming.overlapDeltaMs = -2.25f;
    first.phonemeTiming.attackTimeDeltaMs = 4.f;
    Note second;
    second.id = makeUuid(); second.startTick = 960; second.durationTick = 480;
    second.midiNote = 65; second.lyric = "+";
    original.score.notes = {first, second};
    original.score.sections = {{makeUuid(), "VERSE", 0, 1440}};
    original.score.normalize();
    original.singerId = "builtin:adachi-rei";
    original.phonemizerName = "Japanese Auto";
    original.ppqn = 48; original.runRisingBehavior = 1; original.sectionQuantization = 2;
    original.panelPlaying = true; original.loop = true; original.sectionRange = true;
    original.bpm = 137.5f; original.transpose = -7; original.section = 1;
    original.pitchCvAmount = 0.25f; original.dynamicsCvAmount = -0.5f;
    original.vibratoCvAmount = 0.75f; original.formCvAmount = -0.25f;
    original.editorScrollX = 123.f; original.editorScrollY = 66.f;
    original.editorZoomX = 0.42f; original.editorZoomY = 17.f;
    original.editorFollowPlayhead = false; original.editorSnapEnabled = false;
    original.editorSnapTick = 37;

    const auto nativeJson = projectToJson(original, true);
    const auto decoded = projectFromJson(nativeJson);
    CHECK(scoreToJson(decoded.score) == scoreToJson(original.score));
    CHECK(decoded.singerId == original.singerId); CHECK(decoded.ppqn == 48);
    CHECK(decoded.loop && decoded.sectionRange && decoded.panelPlaying);
    NEAR(decoded.dynamicsCvAmount, -0.5f, 0.0001f);
    CHECK(!decoded.editorSnapEnabled); CHECK(decoded.editorSnapTick == 37);
    const auto nativePath = fs::temp_directory_path() / ("roundtrip-" + makeUuid() + ".vocalrack");
    saveProjectFile(nativePath, original);
    const auto fromDisk = loadProjectFile(nativePath);
    CHECK(projectToJson(fromDisk, false) == projectToJson(decoded, false));
    fs::remove(nativePath);

    const auto exported = exportUstx(original.score);
    CHECK(exported.text.find("renderer: \"CLASSIC\"") != std::string::npos);
    CHECK(exported.text.find("phoneme_overrides") != std::string::npos);
    CHECK(!exported.nativeOnly.empty());
    const auto ustxPath = fs::temp_directory_path() / ("roundtrip-" + makeUuid() + ".ustx");
    writeText(ustxPath, exported.text);
    const auto imported = UstxImporter{}.importTrack(ustxPath, 0);
    fs::remove(ustxPath);
    CHECK(imported.score.notes.size() == 2);
    CHECK(imported.score.title == original.score.title);
    NEAR(imported.score.nominalBpm, 137.5, 0.0001);
    CHECK(imported.score.beatsPerBar == 3);
    const auto& importedFirst = imported.score.notes[0];
    CHECK(importedFirst.startTick == first.startTick);
    CHECK(importedFirst.durationTick == first.durationTick);
    CHECK(importedFirst.midiNote == first.midiNote);
    CHECK(importedFirst.lyric == first.lyric);
    CHECK(importedFirst.phonemeOverrides == first.phonemeOverrides);
    CHECK(!importedFirst.aliasOverride);
    CHECK(importedFirst.pitchSnapFirst == first.pitchSnapFirst);
    CHECK(importedFirst.phonemeTiming.positionOffsetTick == -14);
    CHECK(importedFirst.phonemeTiming.internalPositionOffsetTicks ==
          std::vector<int64_t>({24, -11}));
    NEAR(*importedFirst.phonemeTiming.preutteranceDeltaMs, 9.5f, 0.001f);
    NEAR(*importedFirst.phonemeTiming.overlapDeltaMs, -2.25f, 0.001f);
    CHECK(!importedFirst.phonemeTiming.attackTimeDeltaMs);
    for (const int64_t tick : {0LL, 120LL, 240LL, 480LL, 720LL})
        NEAR(importedFirst.pitchCents.sample(tick), first.pitchCents.sample(tick), 0.6f);
    for (const int64_t tick : {0LL, 180LL, 360LL, 540LL})
        NEAR(importedFirst.dynamicsDb.sample(tick), first.dynamicsDb.sample(tick), 0.11f);
    NEAR(importedFirst.vibrato.startPercent, first.vibrato.startPercent, 0.01f);
    NEAR(importedFirst.vibrato.depthCents, first.vibrato.depthCents, 0.01f);
    NEAR(importedFirst.vibrato.rateHz, first.vibrato.rateHz, 0.01f);
    NEAR(importedFirst.vibrato.phase, first.vibrato.phase, 0.01f);

    auto escapedScore = original.score;
    escapedScore.title = R"(Quoted "title" \ tab)";
    escapedScore.notes[0].lyric = "say \"hi\",\n\\ now";
    escapedScore.notes[0].phonemeOverrides = {R"(alias, with "quotes" and \ slash)"};
    const auto escapedPath = fs::temp_directory_path() / ("escaped-" + makeUuid() + ".ustx");
    writeText(escapedPath, exportUstx(escapedScore).text);
    const auto escapedImported = UstxImporter{}.importTrack(escapedPath, 0).score;
    fs::remove(escapedPath);
    CHECK(escapedImported.title == escapedScore.title);
    CHECK(escapedImported.notes[0].lyric == escapedScore.notes[0].lyric);
    CHECK(escapedImported.notes[0].phonemeOverrides == escapedScore.notes[0].phonemeOverrides);
}

static void feedClock(ClockEstimator& clock, int frames, int interval, double sampleRate, int ppqn) {
    for (int i = 0; i < frames; ++i) clock.process(i % interval == 0 ? 10.f : 0.f, sampleRate, ppqn);
}

static void testClockAndTransport() {
    ClockEstimator clock; feedClock(clock, 6000, 1000, 48000, 24); NEAR(clock.bpm(100), 120.0, 0.01); CHECK(clock.edgeCount() == 6);
    VocalScore score; Note only; only.id = makeUuid(); only.startTick = 0; only.durationTick = 480; only.midiNote = 60; only.lyric = "あ"; score.notes.push_back(only);
    score.sections = {{makeUuid(), "A", 0, 240}, {makeUuid(), "B", 240, 480}}; score.normalize();
    VocalTransport transport; transport.settings.internalBpm = 120; transport.setScore(&score);
    TransportInput input; input.panelPlaying = true; int ends = 0;
    for (int i = 0; i < 25000; ++i) if (transport.process(input, 48000).endPulse) ++ends;
    CHECK(ends == 1); CHECK(!transport.isRunning()); NEAR(transport.positionTick(), 0.0, 0.001);

    transport.reset(); input.runConnected = true; input.run = 10.f;
    for (int i = 0; i < 1000; ++i) transport.process(input, 48000);
    const double held = transport.positionTick(); input.run = 0.f;
    for (int i = 0; i < 1000; ++i) transport.process(input, 48000);
    NEAR(transport.positionTick(), held, 1e-9); input.run = 10.f;
    for (int i = 0; i < 1000; ++i) transport.process(input, 48000);
    CHECK(transport.positionTick() > held);
    input.reset = 10.f; transport.process(input, 48000); CHECK(transport.positionTick() < 0.1); input.reset = 0.f;

    transport.settings.loop = true; transport.reset(); input.runConnected = false; input.panelPlaying = true; ends = 0;
    for (int i = 0; i < 73000; ++i) if (transport.process(input, 48000).endPulse) ++ends;
    CHECK(ends >= 3);

    transport.settings.rangeMode = RangeMode::Section; transport.settings.sectionQuantization = SectionQuantization::EndOfSection;
    transport.settings.selectedSection = 0; transport.setScore(&score); transport.settings.loop = true;
    transport.requestSection(1); input.panelPlaying = true;
    bool switched = false; for (int i = 0; i < 13000; ++i) { auto o = transport.process(input, 48000); if (o.selectedSection == 1) switched = true; }
    CHECK(switched);

    // Jittered clock edges may steer phase, but must never snap the playhead.
    auto longScore = score; longScore.notes[0].durationTick = 48000; longScore.sections.clear(); longScore.normalize();
    VocalTransport synced; synced.settings.internalBpm = 120; synced.settings.ppqn = 24; synced.setScore(&longScore);
    TransportInput clocked; clocked.panelPlaying = true; clocked.clockConnected = true;
    const int edges[] = {0, 1000, 2018, 3009, 4022, 5015};
    size_t nextEdge = 0; double previous = 0.0, largestAdvance = 0.0;
    for (int frame = 0; frame < 6000; ++frame) {
        clocked.clock = nextEdge < std::size(edges) && frame == edges[nextEdge] ? 10.f : 0.f;
        if (clocked.clock > 0.f) ++nextEdge;
        const auto o = synced.process(clocked, 48000);
        largestAdvance = std::max(largestAdvance, o.playheadTick - previous); previous = o.playheadTick;
    }
    CHECK(largestAdvance <= (120.0 * kTicksPerQuarter / (60.0 * 48000.0)) * 1.026);
}

static void testModulation() {
    RealtimeVoiceModulation dsp; ModulationControls c;
    float clean = 0.f, modulated = 0.f;
    for (int i = 0; i < 20000; ++i) clean += std::abs(dsp.process(std::sin(i * 0.03f) * 0.2f, c, 48000));
    dsp.reset(); c.dynamicsCv = 5.f; c.dynamicsAttenuverter = 1.f; c.pitchCv = 5.f; c.pitchAttenuverter = 1.f;
    c.vibratoCv = 5.f; c.vibratoAttenuverter = 1.f; c.formCv = 5.f; c.formAttenuverter = 1.f;
    for (int i = 0; i < 20000; ++i) { const float v = dsp.process(std::sin(i * 0.03f) * 0.2f, c, 48000); CHECK(std::isfinite(v)); CHECK(std::abs(v) <= 1.f); modulated += std::abs(v); }
    CHECK(std::abs(modulated - clean) > 1.f); CHECK(dsp.smoothedDynamicsDb() > 10.f);
    c = {};
    c.pitchCv = std::numeric_limits<float>::quiet_NaN(); c.pitchAttenuverter = 1.f;
    CHECK(std::isfinite(dsp.process(0.2f, c, 48000)));
    c = {};
    for (int i = 0; i < 32; ++i) CHECK(std::isfinite(dsp.process(0.2f, c, 48000)));
}

static void testRenderKeys() {
    auto root = makeTempBank(); auto bank = Voicebank::load(root, "cache-test"); auto score = makeDroneScore(); RenderOptions options;
    auto a = makeRenderKey(score, bank, options), b = makeRenderKey(score, bank, options); CHECK(a == b);
    ++score.revision; auto c = makeRenderKey(score, bank, options); CHECK(!(a == c));
    auto changedBank = bank; ++changedBank.contentRevision;
    CHECK(!(makeRenderKey(score, bank, options) == makeRenderKey(score, changedBank, options)));
    RenderCache cache; auto audio = std::make_shared<RenderedAudio>(); cache.put(a, audio); CHECK(cache.get(a)); CHECK(cache.hits() == 1);
    CHECK(!cache.get(c)); CHECK(cache.misses() == 1); cache.invalidateSinger("cache-test"); CHECK(!cache.get(a));
    auto oversized = makeDroneScore();
    oversized.notes.front().durationTick = 1000000;
    oversized.normalize(); oversized.touch();
    auto slot = RenderService::instance().createSlot();
    options.sampleRate = 48000; options.bpm = 120;
    RenderService::instance().submit(slot, oversized, bank, options);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (slot->rendering.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(!slot->rendering.load(std::memory_order_acquire));
    CHECK(slot->renderFailed.load(std::memory_order_acquire));
    CHECK(!slot->audio());
    RenderService::instance().cancel(slot);
    fs::remove_all(root);
}

static size_t longestQuietRun(const AudioBuffer& audio, float threshold) {
    size_t longest = 0, current = 0;
    for (float v : audio.samples) { if (std::abs(v) < threshold) longest = std::max(longest, ++current); else current = 0; }
    return longest;
}

static size_t longestLoudRun(const AudioBuffer& audio, float threshold) {
    size_t longest = 0, current = 0;
    for (float v : audio.samples) {
        if (std::abs(v) > threshold) longest = std::max(longest, ++current);
        else current = 0;
    }
    return longest;
}

static double windowRms(const AudioBuffer& audio, double centerSeconds, double radiusSeconds) {
    const size_t begin = static_cast<size_t>(std::max(0.0, centerSeconds - radiusSeconds) * audio.sampleRate);
    const size_t end = std::min(audio.samples.size(), static_cast<size_t>((centerSeconds + radiusSeconds) * audio.sampleRate));
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) sum += audio.samples[i] * audio.samples[i];
    return end > begin ? std::sqrt(sum / (end - begin)) : 0.0;
}

static double estimatePitchNear(const AudioBuffer& audio, size_t begin, size_t end, double target) {
    begin = std::min(begin, audio.samples.size());
    end = std::min(end, audio.samples.size());
    if (end <= begin + 512 || target <= 0.0) return 0.0;
    const size_t window = std::min<size_t>(8192, end - begin);
    begin += ((end - begin) - window) / 2;
    end = begin + window;
    const size_t firstLag = static_cast<size_t>(std::floor(audio.sampleRate / (target * 1.06)));
    const size_t lastLag = static_cast<size_t>(std::ceil(audio.sampleRate / (target * 0.94)));
    size_t bestLag = firstLag;
    double best = std::numeric_limits<double>::infinity();
    for (size_t lag = firstLag; lag <= lastLag; ++lag) {
        double difference = 0.0, energy = 0.0;
        for (size_t i = begin; i + lag < end; ++i) {
            const double a = audio.samples[i], b = audio.samples[i + lag];
            const double delta = a - b;
            difference += delta * delta;
            energy += a * a + b * b;
        }
        const double normalized = difference / std::max(energy, 1e-18);
        if (normalized < best) { best = normalized; bestLag = lag; }
    }
    return bestLag ? audio.sampleRate / static_cast<double>(bestLag) : 0.0;
}

static void testOfficialOffline(const fs::path& singerPath) {
    auto singer = Voicebank::load(singerPath, "builtin:adachi-rei");
    CHECK(singer.valid()); CHECK(singer.character.name == "足立レイ"); CHECK(singer.entries.size() > 600);
    for (const auto& entry : singer.entries) {
        CHECK(entry.valid); CHECK(fs::is_regular_file(entry.wavPath)); CHECK(!entry.alias.empty());
        CHECK(std::isfinite(entry.offsetMs)); CHECK(std::isfinite(entry.consonantMs));
        CHECK(std::isfinite(entry.cutoffMs)); CHECK(std::isfinite(entry.preutterMs));
        CHECK(std::isfinite(entry.overlapMs));
    }
    NEAR(singer.referencePitchHz, 278.17, 2.0);
    auto score = makeJapaneseFirstSoundScore(); NativeV1Renderer renderer; RenderOptions options; options.sampleRate = 48000; options.bpm = 120;
    options.phonemizer = kJapaneseAutoPhonemizer;
    auto at120 = renderer.render(score, singer, options); CHECK(finiteAudio(at120)); CHECK(peakAudio(at120) > 0.01f); CHECK(at120.diagnostics.errors.empty());
    CHECK(at120.diagnostics.phonemes.size() == score.notes.size());
    for (const auto& phone : at120.diagnostics.phonemes) {
        CHECK(phone.sourcePitchHz > 230.0);
        CHECK(phone.sourcePitchHz < 330.0);
    }

    auto englishScore = makeDefaultScore();
    options.phonemizer = kEnglishToJapanesePhonemizer;
    auto accentedEnglish = renderer.render(englishScore, singer, options);
    CHECK(finiteAudio(accentedEnglish)); CHECK(peakAudio(accentedEnglish) > 0.01f);
    CHECK(accentedEnglish.diagnostics.errors.empty());
    CHECK(accentedEnglish.diagnostics.phonemes.size() > englishScore.notes.size());
    for (const auto& englishTemplate : std::vector<VocalScore>{
             makeEnglishDroneScore(), makeEnglishTriggeredWordScore(), makeEnglishLoopPhraseScore()}) {
        const auto renderedTemplate = renderer.render(englishTemplate, singer, options);
        CHECK(finiteAudio(renderedTemplate));
        CHECK(peakAudio(renderedTemplate) > 0.01f);
        CHECK(renderedTemplate.diagnostics.errors.empty());
        CHECK(renderedTemplate.diagnostics.phonemes.size() >= englishTemplate.notes.size());
    }
    // A whole-word non-silence check can pass even when a medial phoneme is
    // dropped. Prove the built-in `star` template resolves and mixes
    // all three English-to-Japanese events: す・た・う.
    auto englishPhrase = makeEnglishLoopPhraseScore();
    const auto star = std::find_if(englishPhrase.notes.begin(), englishPhrase.notes.end(),
        [](const Note& note) { return note.lyric == "star"; });
    CHECK(star != englishPhrase.notes.end());
    const auto renderedPhrase = renderer.render(englishPhrase, singer, options);
    std::vector<const PhonemeEvent*> starPhones;
    for (const auto& phone : renderedPhrase.diagnostics.phonemes)
        if (phone.sourceNoteId == star->id) starPhones.push_back(&phone);
    CHECK(starPhones.size() == 3);
    CHECK(starPhones[0]->requestedAlias == "す");
    CHECK(starPhones[1]->requestedAlias == "た");
    CHECK(starPhones[2]->requestedAlias == "う");
    for (const auto* phone : starPhones) {
        CHECK(phone->oto != nullptr);
        CHECK(phone->automaticRelativeTick.has_value());
        CHECK(phone->renderedFrames > 1000);
        CHECK(phone->renderedRms > 0.005f);
    }
    // The editor's internal white divider uses the same indexed operation as
    // the renderer. Moving the た onset must leave す and う in place, change
    // the rendered audio, and retain the phonemizer-authored base tick for a
    // subsequent drag instead of applying the offset twice.
    const size_t starIndex = static_cast<size_t>(star - englishPhrase.notes.begin());
    const int64_t naturalSuTick = starPhones[0]->relativeTick;
    const int64_t naturalTaTick = starPhones[1]->relativeTick;
    const int64_t naturalUTick = starPhones[2]->relativeTick;
    const std::vector<int64_t> starAutomaticTicks{
        *starPhones[0]->automaticRelativeTick,
        *starPhones[1]->automaticRelativeTick,
        *starPhones[2]->automaticRelativeTick,
    };
    CHECK(setInternalPhonemeBoundaryTick(englishPhrase.notes[starIndex],
                                         starAutomaticTicks, 1,
                                         starAutomaticTicks[1] + 60));
    englishPhrase.touch();
    const auto shiftedPhrase = renderer.render(englishPhrase, singer, options);
    std::vector<const PhonemeEvent*> shiftedStarPhones;
    for (const auto& phone : shiftedPhrase.diagnostics.phonemes)
        if (phone.sourceNoteId == star->id) shiftedStarPhones.push_back(&phone);
    CHECK(shiftedStarPhones.size() == 3);
    CHECK(shiftedStarPhones[0]->relativeTick == naturalSuTick);
    CHECK(shiftedStarPhones[1]->relativeTick == naturalTaTick + 60);
    CHECK(shiftedStarPhones[2]->relativeTick == naturalUTick);
    CHECK(*shiftedStarPhones[1]->automaticRelativeTick == starAutomaticTicks[1]);
    double internalBoundaryDifference = 0.0;
    for (size_t frame = 0; frame < std::min(renderedPhrase.samples.size(),
                                             shiftedPhrase.samples.size()); ++frame)
        internalBoundaryDifference += std::abs(
            renderedPhrase.samples[frame] - shiftedPhrase.samples[frame]);
    CHECK(internalBoundaryDifference /
        std::max<size_t>(1, renderedPhrase.samples.size()) > 0.0001);

    // Timing controls belong to the authored note that owns each resolved
    // event, including a coda allocated onto an English continuation note.
    // This specifically guards the editor case where `star` is followed by
    // `+`: moving the visible final-phone bar must alter rendered audio.
    VocalScore continuedStar;
    Note starRoot;
    starRoot.id = "continued-star-root";
    starRoot.startTick = 0;
    starRoot.durationTick = 960;
    starRoot.midiNote = 57;
    starRoot.lyric = "star";
    Note starHold;
    starHold.id = "continued-star-hold";
    starHold.startTick = 960;
    starHold.durationTick = 480;
    starHold.midiNote = 55;
    starHold.lyric = "+";
    continuedStar.notes = {starRoot, starHold};
    continuedStar.normalize();
    const auto naturalContinuedStar = renderer.render(continuedStar, singer, options);
    CHECK(naturalContinuedStar.diagnostics.errors.empty());
    const auto naturalCoda = std::find_if(
        naturalContinuedStar.diagnostics.phonemes.begin(),
        naturalContinuedStar.diagnostics.phonemes.end(),
        [&](const PhonemeEvent& phone) { return phone.sourceNoteId == starHold.id; });
    CHECK(naturalCoda != naturalContinuedStar.diagnostics.phonemes.end());
    continuedStar.notes[1].phonemeTiming.positionOffsetTick = -120;
    continuedStar.touch();
    const auto shiftedContinuedStar = renderer.render(continuedStar, singer, options);
    CHECK(shiftedContinuedStar.diagnostics.errors.empty());
    const auto shiftedCoda = std::find_if(
        shiftedContinuedStar.diagnostics.phonemes.begin(),
        shiftedContinuedStar.diagnostics.phonemes.end(),
        [&](const PhonemeEvent& phone) { return phone.sourceNoteId == starHold.id; });
    CHECK(shiftedCoda != shiftedContinuedStar.diagnostics.phonemes.end());
    CHECK(shiftedCoda->renderedFrames > 1000);
    CHECK(shiftedCoda->renderedRms > 0.005f);
    double continuedTimingDifference = 0.0;
    for (size_t frame = 0; frame < std::min(naturalContinuedStar.samples.size(),
                                             shiftedContinuedStar.samples.size()); ++frame)
        continuedTimingDifference += std::abs(
            naturalContinuedStar.samples[frame] - shiftedContinuedStar.samples[frame]);
    CHECK(continuedTimingDifference /
        std::max<size_t>(1, naturalContinuedStar.samples.size()) > 0.0001);

    // Broad production-bank pronunciation matrix. A whole-file "non-silent"
    // assertion would miss internal dropouts, so every event of every
    // word must resolve and make a measurable contribution. The vocabulary
    // spans vowels, onset/coda consonants, clusters, diphthongs, affricates,
    // multi-syllable words, connected notes, rests, registers, and three
    // durations of the formerly failing `star` case.
    const std::vector<std::string> pronunciationWords = {
        "a", "and", "are", "be", "can", "do", "english", "for",
        "hello", "hi", "i", "in", "is", "it", "me", "my", "no",
        "of", "on", "openutau", "all", "an", "barn", "cute", "itch",
        "its", "read", "rack", "sing", "soon", "star", "tea", "test",
        "testing", "the", "this", "to", "utau", "vocal", "vocalrack",
        "voice", "vu", "we", "with", "words", "you", "up",
        "pack", "bad", "tap", "dad", "kick", "gig", "cake", "dog",
        "fan", "van", "thin", "then", "sip", "zip", "fish", "vision",
        "chip", "judge", "match", "ring", "moon", "name", "light", "red",
        "yes", "water", "world", "day", "night", "boy", "now", "go",
        "loud", "blue", "green", "play", "train", "street", "spring",
        "sky", "glow", "melody", "harmony", "music", "robot", "computer",
        "beautiful", "forever", "together", "synthesizer", "turn", "again",
        "stars", "bright", "take", "breath", "put", "down",
    };
    VocalScore pronunciationMatrix;
    pronunciationMatrix.title = "English pronunciation contribution matrix";
    int64_t matrixTick = 240;
    for (size_t index = 0; index < pronunciationWords.size(); ++index) {
        Note matrixNote;
        matrixNote.id = "word-" + std::to_string(index) + "-" + pronunciationWords[index];
        matrixNote.startTick = matrixTick;
        matrixNote.durationTick = 480;
        matrixNote.midiNote = std::vector<int>{43, 55, 67}[index % 3];
        matrixNote.lyric = pronunciationWords[index];
        pronunciationMatrix.notes.push_back(matrixNote);
        // Alternate connected joins and explicit rests.
        matrixTick += matrixNote.durationTick + (index % 2 ? 120 : 0);
    }
    for (const int64_t duration : {240LL, 960LL, 1920LL}) {
        Note durationStar;
        durationStar.id = "star-duration-" + std::to_string(duration);
        durationStar.startTick = matrixTick + 120;
        durationStar.durationTick = duration;
        durationStar.midiNote = duration == 240 ? 67 : duration == 960 ? 55 : 43;
        durationStar.lyric = "star";
        pronunciationMatrix.notes.push_back(durationStar);
        matrixTick = durationStar.endTick() + 120;
    }
    pronunciationMatrix.normalize();
    const auto renderedMatrix = renderer.render(pronunciationMatrix, singer, options);
    CHECK(finiteAudio(renderedMatrix));
    for (const auto& error : renderedMatrix.diagnostics.errors)
        std::cerr << "English matrix render error: " << error << '\n';
    CHECK(renderedMatrix.diagnostics.errors.empty());
    for (const auto& matrixNote : pronunciationMatrix.notes) {
        std::vector<const PhonemeEvent*> phones;
        for (const auto& phone : renderedMatrix.diagnostics.phonemes)
            if (phone.sourceNoteId == matrixNote.id) phones.push_back(&phone);
        CHECK(!phones.empty());
        int64_t priorTick = std::numeric_limits<int64_t>::min();
        for (const auto* phone : phones) {
            CHECK(phone->relativeTick >= priorTick);
            priorTick = phone->relativeTick;
            CHECK(phone->oto != nullptr);
            CHECK(!phone->selectedAlias.empty());
            // Plosive bridges may be only 55 ticks and mostly closure. Keep a
            // hard nonzero energy floor (missing phones remain exactly zero)
            // without requiring those consonants to equal a sustained vowel.
            constexpr float minimumPhoneRms = 0.0001f;
            if (phone->renderedFrames <= 64 || phone->renderedRms <= minimumPhoneRms)
                std::cerr << "English matrix weak phone: lyric=" << matrixNote.lyric
                          << " requested=" << phone->requestedAlias
                          << " selected=" << phone->selectedAlias
                          << " tick=" << phone->relativeTick
                          << " frames=" << phone->renderedFrames
                          << " rms=" << phone->renderedRms << '\n';
            CHECK(phone->renderedFrames > 64);
            CHECK(phone->renderedRms > minimumPhoneRms);
        }
    }
    options.phonemizer = kJapaneseAutoPhonemizer;
    // The lightweight single-window estimator remains in use for
    // source-bank fallback and the steady drone below. Polyphonic-looking
    // PSOLA/formant spectra require a tracked estimator; all phrase notes are
    // therefore gated by the Dockerized pYIN comparison instead of a brittle
    // one-frame harmonic choice here.
    NEAR(at120.samples.size() / 48000.0, 3.5, 0.02); CHECK(longestQuietRun(at120, 1e-6f) < 48000 / 8);
    auto deterministic = renderer.render(score, singer, options); CHECK(at120.samples == deterministic.samples);
    // Rack consumes RenderService's bar-sized assembly, not the direct
    // renderer above. Keep the exact real-Rack 44.1 kHz phrase path under a
    // continuity gate so a chunked buffer cannot hide a mid-word dropout even
    // when the transport itself reaches END at the correct time.
    RenderOptions serviceOptions = options;
    serviceOptions.sampleRate = 44100;
    auto serviceSlot = RenderService::instance().createSlot();
    const auto canceledBefore = RenderService::instance().stats().canceledJobs;
    uint64_t serviceGeneration = RenderService::instance().submit(serviceSlot, score, singer, serviceOptions);
    // Rack can issue several identical startup requests while its sample rate,
    // patch state, and first external-clock estimate settle. Each generation
    // needs a private cancellation token: reusing one flag once allowed a
    // canceled, silent final chunk to enter the cache and mute the last mora
    // until the next tempo rerender.
    const auto activeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (RenderService::instance().stats().activeJobs == 0 &&
           std::chrono::steady_clock::now() < activeDeadline)
        std::this_thread::yield();
    for (int replacement = 0; replacement < 6; ++replacement) {
        serviceGeneration = RenderService::instance().submit(serviceSlot, score, singer, serviceOptions);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto serviceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (serviceSlot->rendering.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < serviceDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(!serviceSlot->rendering.load(std::memory_order_acquire));
    CHECK(serviceSlot->completedGeneration.load(std::memory_order_acquire) == serviceGeneration);
    CHECK(RenderService::instance().stats().canceledJobs > canceledBefore);
    const RenderedAudio* serviceAudio = serviceSlot->audio();
    CHECK(serviceAudio); CHECK(finiteAudio(*serviceAudio)); CHECK(serviceAudio->diagnostics.errors.empty());
    CHECK(longestQuietRun(*serviceAudio, 1e-4f) < serviceOptions.sampleRate / 10);
    RenderService::instance().cancel(serviceSlot);
    // Short, adjacent morae exercise PSOLA at both ends of each tiny event.
    // A near-zero overlap weight once produced one enormous quotient here;
    // the DC blocker then held the soft limiter at full scale for ~195 ms.
    VocalScore shortPhrase;
    const std::vector<std::string> shortLyrics = {"あ", "だ", "ち", "れ", "い", "う"};
    const std::vector<int> shortTones = {55, 57, 59, 60, 59, 55};
    for (size_t i = 0; i < shortLyrics.size(); ++i) {
        Note shortNote;
        shortNote.id = makeUuid();
        shortNote.startTick = static_cast<int64_t>(i) * 120;
        shortNote.durationTick = 120;
        shortNote.midiNote = shortTones[i];
        shortNote.lyric = shortLyrics[i];
        shortPhrase.notes.push_back(std::move(shortNote));
    }
    shortPhrase.normalize();
    const auto shortAudio = renderer.render(shortPhrase, singer, options);
    CHECK(finiteAudio(shortAudio)); CHECK(shortAudio.diagnostics.errors.empty());
    CHECK(peakAudio(shortAudio) < 0.75f);
    CHECK(longestLoudRun(shortAudio, 0.70f) < options.sampleRate / 1000);
    auto timingScore = score;
    timingScore.notes[1].phonemeTiming.positionOffsetTick = -18;
    timingScore.notes[1].phonemeTiming.preutteranceDeltaMs = 35.f;
    timingScore.notes[1].phonemeTiming.overlapDeltaMs = 12.f;
    timingScore.notes[1].phonemeTiming.attackTimeDeltaMs = 4.f;
    timingScore.touch();
    const auto timingAudio = renderer.render(timingScore, singer, options);
    CHECK(finiteAudio(timingAudio)); CHECK(timingAudio.diagnostics.errors.empty());
    double timingDifference = 0.0;
    for (size_t i = 0; i < std::min(at120.samples.size(), timingAudio.samples.size()); ++i)
        timingDifference += std::abs(at120.samples[i] - timingAudio.samples[i]);
    CHECK(timingDifference / std::max<size_t>(1, at120.samples.size()) > 0.0001);
    auto legacyScore = UstxImporter{}.importTrack("tests/fixtures/legacy_song.ust", 0).score;
    auto legacyAudio = renderer.render(legacyScore, singer, options);
    CHECK(finiteAudio(legacyAudio)); CHECK(legacyAudio.diagnostics.errors.empty());
    CHECK(legacyAudio.diagnostics.phonemes.size() == legacyScore.notes.size());
    CHECK(peakAudio(legacyAudio) > 0.01f); CHECK(rmsAudio(legacyAudio) > 0.01f);
    size_t continued = 0;
    for (const auto& phone : legacyAudio.diagnostics.phonemes)
        if (phone.diagnostic.rfind("Continued ", 0) == 0) ++continued;
    CHECK(continued == 3);
    // A tied continuation remains audible across its note boundary instead
    // of applying a second consonant attack or falling into silence.
    CHECK(windowRms(legacyAudio, 2.0, 0.015) > 0.015);
    CHECK(windowRms(legacyAudio, 4.25, 0.015) > 0.015);
    options.bpm = 60; auto at60 = renderer.render(score, singer, options); NEAR(at60.samples.size() / static_cast<double>(at120.samples.size()), 2.0, 0.01);
    auto drone = makeDroneScore(); options.bpm = 120; options.transposeSemitones = 0; auto base = renderer.render(drone, singer, options);
    options.transposeSemitones = 7; auto up = renderer.render(drone, singer, options);
    const auto begin = static_cast<size_t>(0.4 * options.sampleRate), end = static_cast<size_t>(1.6 * options.sampleRate);
    const double baseTarget = 261.625565, upTarget = baseTarget * std::pow(2.0, 7.0 / 12.0);
    const double basePitch = estimatePitchNear(base, begin, end, baseTarget);
    const double upPitch = estimatePitchNear(up, begin, end, upTarget);
    std::cout << "INFO  measured drone pitch base=" << basePitch << " Hz, +7=" << upPitch << " Hz\n";
    NEAR(basePitch, baseTarget, 5.0); NEAR(upPitch, upTarget, 5.0);
    options.transposeSemitones = -36; auto lowest = renderer.render(drone, singer, options);
    options.transposeSemitones = 36; auto highest = renderer.render(drone, singer, options);
    CHECK(finiteAudio(lowest)); CHECK(finiteAudio(highest));
    CHECK(peakAudio(lowest) > 0.005f); CHECK(peakAudio(highest) > 0.005f);
    CHECK(longestQuietRun(lowest, 1e-6f) < options.sampleRate / 8);
    CHECK(longestQuietRun(highest, 1e-6f) < options.sampleRate / 8);
    const double lowPitch = estimatePitchNear(lowest, begin, end, 40.0);
    const double highPitch = estimatePitchNear(highest, begin, end, 2000.0);
    std::cout << "INFO  measured extreme transpose pitch -36=" << lowPitch
              << " Hz, +36=" << highPitch << " Hz\n";
    CHECK(lowPitch < basePitch * 0.3); CHECK(highPitch > basePitch * 3.0);
    options.sampleRate = 96000; auto hiRate = renderer.render(drone, singer, options); CHECK(hiRate.sampleRate == 96000); CHECK(finiteAudio(hiRate));
}

int main(int argc, char** argv) {
    fs::path singer; bool offline = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--singer" && i + 1 < argc) singer = argv[++i];
        else if (std::string(argv[i]) == "--offline") offline = true;
    }
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"encoding", testEncoding}, {"editor navigation", testEditorNavigation},
        {"monophonic overwrite editing", testMonophonicOverwrite},
        {"voicebank and phonemizers", testVoicebankAndPhonemizers},
        {"score serialization and migration", testScoreSerialization}, {"USTX import", testUstx},
        {"VocalRack and OpenUtau round trip", testProjectAndUstxRoundTrip},
        {"clock and transport", testClockAndTransport}, {"realtime modulation", testModulation}, {"render keys/cache", testRenderKeys}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS  " << test.first << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL  " << test.first << ": " << e.what() << '\n'; }
    }
    if (offline) {
        try { CHECK(!singer.empty()); testOfficialOffline(singer); std::cout << "PASS  official Adachi Rei offline renderer\n"; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL  official Adachi Rei offline renderer: " << e.what() << '\n'; }
    }
    std::cout << (failed ? "FAILED " : "PASSED ") << (tests.size() + (offline ? 1 : 0)) << " test groups, failures=" << failed << '\n';
    return failed ? 1 : 0;
}
