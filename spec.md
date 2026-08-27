# VocalRack V1 Implementation Specification

**Status:** Implementation handoff specification
**Working project name:** VocalRack
**Primary module name:** VOCAL
**Secondary module name:** SINGER PLATE
**Target platform:** VCV Rack 2
**Source-code license target:** MIT
**Distribution model:** Free of charge

---

## 1. Implementation Directive

Implement this specification completely. Do not stop after scaffolding, mockups, a proof of concept, or an offline WAV renderer. Continue building, running, testing, and correcting the project until every required V1 feature and every mandatory acceptance test in this document passes.

The project is not complete unless the actual VCV Rack module has been end-to-end tested inside VCV Rack and the resulting test artifacts have been saved.

---

## 2. Product Definition

VocalRack is a native VCV Rack singing-synthesizer plugin with UTAU voicebank compatibility.

Its purpose is to make the following workflow possible without a DAW:

```text
write or import a vocal score
        -> choose a singer
        -> synchronize it to a Rack clock
        -> modulate vocal expression with CV
        -> output continuous vocal audio
        -> process it with ordinary Rack effects and mixers
```

The primary abstraction is:

> One VOCAL module contains one independent vocal performance of arbitrary length.

That performance may be:

- one sustained vowel;
- one word;
- one short phrase;
- a collection of selectable phrases represented as sections;
- a backing-vocal part;
- a chorus;
- or a complete multi-minute song.

The engine must represent a word, phrase, or song as the same score type. Only the score length differs.

Multiple instances are expected and must be supported. Examples include:

```text
VOCAL 1: full lead song
VOCAL 2: harmony, transpose +4 semitones
VOCAL 3: sustained "oo" backing layer
VOCAL 4: randomly selected short phrases
```

The plugin must support both of these equally important use cases:

1. **Complete vocal-song workstation:** Author or import an entire UTAU/OpenUtau-style vocal track and play it in sync with a Rack patch.
2. **Modular vocal sound source:** Use one or more short vocal scores as triggered, looped, randomized, transposed, and heavily processed modular sound sources.

Neither use case may be treated as an afterthought.

---

## 3. V1 Product Goals

V1 must prove all of the following:

1. A fresh install can add VOCAL to a patch and use bundled Adachi Rei without separate voicebank setup.
2. Users can author a complete English or Japanese score inside the module without OpenUtau.
3. Importing a supported OpenUtau `.ustx` track creates the module's internal score.
4. A score can be any length, from one word to a full song.
5. The voice plays as continuous audio rather than as clock-gated fragments.
6. External clock tempo changes the performance speed.
7. RUN, RESET, TRIG, LOOP, one-shot behavior, pause/resume, and section selection behave deterministically.
8. Authored pitch and expression remain intact while incoming CV adds nondestructive variation.
9. Multiple VOCAL instances can run independently in one patch.
10. The expensive vocal rendering work never blocks Rack's audio thread.
11. Saving and reopening a `.vcv` patch restores the complete vocal score and module state.
12. The actual Rack plugin passes the mandatory end-to-end tests in Section 22.

A successful V1 is not required to sound identical to every UTAU/OpenUtau renderer. It must produce intelligible, musically synchronized, usable singing from the bundled singer.

---

## 4. Required Initial Setup

Complete these setup steps before implementing the renderer.

### 4.1 Create the Rack plugin project

- Use the VCV Rack 2 SDK.
- Set the plugin manifest license identifier to `MIT`.
- Keep the Rack plugin free of charge.
- Record the exact Rack SDK version used for development and CI.
- Do not copy significant portions of VCV Rack source into this project.

### 4.2 Clone OpenUtau as a local reference implementation

Clone the official repository:

```bash
git clone https://github.com/openutau/OpenUtau.git reference/OpenUtau
git -C reference/OpenUtau rev-parse HEAD > reference/OPENUTAU_COMMIT.txt
```

Requirements:

- The clone is a development reference, not an assumed runtime dependency.
- Inspect it to understand USTX parsing, voicebank loading, `oto.ini`, prefix maps, phonemizer behavior, render-phrase construction, pitch curves, vibrato, pre-rendering, and sample timing.
- Do not blindly translate the entire C# application.
- Reuse or port specific MIT-licensed portions when they provide a documented advantage.
- Preserve all required copyright and license notices for any reused code.
- Record every copied or substantially adapted source file in `THIRD_PARTY_NOTICES.md`.

### 4.3 Download the official Adachi Rei UTAU voicebank

Download the official current standard UTAU voicebank from:

```text
https://mechanicalgirl.jp/adachi-rei/
```

At the time this specification was written, the official page listed **足立レイ ver3.5.0**. Use the official standard voicebank, not a fan voicebank, similarly named bank, unofficial mirror, or the separate A.I.VOICE/レプリボイス product.

Requirements:

- Save the original archive unchanged in a controlled development-assets location.
- Record the download source page, exact download URL, version, date, and SHA-256 hash.
- Inspect and preserve the terms included inside the archive.
- Vendor the files needed by the renderer into the release package so the installed plugin works offline.
- Prefer the character image referenced by the voicebank's own metadata for the default SINGER PLATE image.
- Do not use separately distributed character artwork unless its terms are independently reviewed and retained.
- Add the voicebank's original terms and attribution to the distribution.
- The Adachi Rei voicebank and character assets are not relicensed under MIT.

A release build must fail with a clear message if the required bundled singer assets are absent.

### 4.4 Record provenance

Create:

```text
THIRD_PARTY_NOTICES.md
third_party/adachi_rei/SOURCE.md
reference/OPENUTAU_COMMIT.txt
```

`SOURCE.md` must include:

- official source page;
- exact archive URL;
- version;
- download date;
- SHA-256;
- included license filenames;
- list of files shipped in the plugin;
- any transformations performed on the original files.

---

## 5. Licensing Requirements

### 5.1 Project code

The project-authored source code must use the MIT License.

Add:

```text
LICENSE
```

Use `"license": "MIT"` in `plugin.json`.

### 5.2 Rack distribution condition

Because the Rack plugin is being released under MIT rather than GPL-3.0-or-later, distribute the Rack plugin free of charge under VCV's non-commercial plugin exception. Do not add a required payment, paid unlock, or commercial Rack distribution to V1.

### 5.3 Adachi Rei assets

The bundled Adachi Rei files remain under their original terms.

The repository and package must state:

```text
VocalRack source code: MIT
Bundled Adachi Rei UTAU voicebank and associated assets: original Mechanical Girl terms
```

Do not place an MIT header on the voicebank WAV files, `oto.ini`, character image, or original license documents.

### 5.4 Dependencies

Use only dependencies compatible with an MIT-licensed project and free Rack distribution. Prefer MIT, BSD, ISC, zlib, or public-domain libraries.

Do not add GPL-only code or dependencies that would force the project itself to be GPL, unless the user explicitly changes the license decision in the future.

---

## 6. V1 Deliverables

V1 consists of two user-visible Rack modules and one shared vocal engine.

### 6.1 VOCAL

A wide, full-workstation Rack module containing:

- one singer selection;
- one arbitrary-length vocal score;
- full score editing;
- OpenUtau USTX import;
- selectable English and Japanese phonemization;
- UTAU voicebank rendering;
- clocked transport;
- optional sections;
- loop and one-shot behavior;
- semitone transpose;
- CV expression inputs;
- continuous mono voice output;
- end-of-range trigger output;
- render and error status.

### 6.2 SINGER PLATE

A decorative visual module that:

- defaults to the bundled Adachi Rei singer;
- can select any installed/imported voicebank;
- reads and displays that voicebank's character image;
- optionally displays the singer name;
- has no DSP and no audio ports;
- persists its singer selection and display options.

### 6.3 Shared vocal engine

The core voicebank, phonemizer, score, rendering, cache, and import code must live outside the Rack UI/module classes and be reusable by both the Rack plugin and offline tests.

---

## 7. Explicit V1 Non-Goals

Do not expand V1 to include these unless all required V1 work is already complete and tested:

- third-party phonemizer plugin loading;
- complete OpenUtau compatibility;
- legacy UST import;
- VSQ/VSQX import;
- external Windows UTAU resampler executables;
- multiple selectable renderer backends;
- Classic-versus-Worldline UI;
- renderer-specific tension, breathiness, gender, voice-color, or similar advanced expression systems;
- automatic diatonic harmony generation;
- linked scores shared across module instances;
- multiple singers inside one VOCAL instance;
- stereo vocal rendering;
- built-in effects, mixing, mastering, or DAW-style arrangement tracks;
- a compact alternate VOCAL panel;
- a split Score plus Singer module architecture;
- note gate, phoneme gate, vowel gate, or consonant gate outputs;
- exporting USTX from the module;
- baking live CV into editable score curves;
- audio-rate tempo modulation.

The architecture must leave room for many of these later, but they are not V1 acceptance requirements.

---

## 8. Core Domain Model

Use a fixed musical tick resolution of **480 ticks per quarter note** unless the architecture documents a better representation.

### 8.1 VocalScore

```cpp
struct VocalScore {
    uint32_t schemaVersion;
    std::string title;
    double nominalBpm;
    int beatsPerBar;
    int beatUnit;
    std::vector<Note> notes;
    std::vector<Section> sections;
};
```

Requirements:

- No hard-coded maximum song length.
- Notes and sections use musical ticks, not absolute audio samples.
- A one-word score and a five-minute song use the same type.
- Score edits increment a revision number used for cache invalidation.
- V1 supports one global nominal BPM and one global time signature.
- If an imported USTX contains tempo-map or time-signature changes, preserve note locations in musical ticks, use the first valid global values for internal free-running playback, and show a visible import warning that tempo-map playback is not supported in V1.

### 8.2 Note

```cpp
struct Note {
    Uuid id;
    int64_t startTick;
    int64_t durationTick;
    int midiNote;
    std::string lyric;
    std::optional<std::string> aliasOverride;
    Curve pitchCents;
    Curve dynamicsDb;
    Vibrato vibrato;
};
```

Requirements:

- `midiNote` is the authored base note.
- `pitchCents` is relative to the authored note.
- `dynamicsDb` is an authored expression curve, not the external CV layer.
- `aliasOverride` allows an advanced user to bypass automatic alias choice for a note.
- Notes may not have negative duration.
- Overlapping notes in one VOCAL score are not required in V1; detect and reject or visibly flag them.

### 8.3 Curve

A curve is an ordered list of points relative to its owning note or score range.

```cpp
struct CurvePoint {
    int64_t tickOffset;
    float value;
};
```

Use deterministic interpolation. Piecewise linear interpolation is acceptable for V1.

### 8.4 Vibrato

```cpp
struct Vibrato {
    float startPercent;
    float depthCents;
    float rateHz;
    float phase;
    float fadeInPercent;
    float fadeOutPercent;
};
```

### 8.5 Section

```cpp
struct Section {
    Uuid id;
    std::string name;
    int64_t startTick;
    int64_t endTick;
};
```

Requirements:

- Sections are optional.
- V1 sections are ordered and non-overlapping.
- A section may represent a verse, chorus, word, phrase, sustained vowel, or any other useful range.
- Section names are user editable.

---

## 9. Project Architecture

Use a layered architecture similar to:

```text
Rack UI and ports
        |
        v
VocalModule controller
        |
        +--> Transport / clock estimator
        +--> Realtime modulation DSP
        |
        v
Immutable VocalScore snapshot
        |
        v
Phonemizer
        |
        v
RenderPlan / phoneme events
        |
        v
UTAU voicebank mapper
        |
        v
Background renderer
        |
        v
Rendered chunk cache / ring buffer
        |
        v
Rack audio thread
        |
        v
VOICE OUT
```

Recommended source layout:

```text
VocalRack/
├── spec.md
├── LICENSE
├── THIRD_PARTY_NOTICES.md
├── plugin.json
├── Makefile
├── res/
│   ├── Vocal.svg
│   ├── SingerPlate.svg
│   └── singers/
│       └── adachi-rei/
├── src/
│   ├── plugin.cpp
│   ├── plugin.hpp
│   ├── rack/
│   │   ├── VocalModule.cpp
│   │   ├── VocalModule.hpp
│   │   ├── VocalWidget.cpp
│   │   ├── VocalDisplay.cpp
│   │   ├── VocalEditor.cpp
│   │   ├── SingerPlateModule.cpp
│   │   └── SingerPlateWidget.cpp
│   ├── core/
│   │   ├── VocalScore.*
│   │   ├── Note.*
│   │   ├── Curve.*
│   │   ├── Section.*
│   │   └── Serialization.*
│   ├── voicebank/
│   │   ├── Voicebank.*
│   │   ├── VoicebankLoader.*
│   │   ├── OtoParser.*
│   │   ├── PrefixMap.*
│   │   └── CharacterMetadata.*
│   ├── phonemizer/
│   │   ├── IPhonemizer.*
│   │   ├── JapaneseAutoPhonemizer.*
│   │   └── DirectAliasPhonemizer.*
│   ├── render/
│   │   ├── RenderPlan.*
│   │   ├── IVocalRenderer.*
│   │   ├── NativeV1Renderer.*
│   │   ├── RenderService.*
│   │   ├── RenderCache.*
│   │   └── AudioRingBuffer.*
│   ├── transport/
│   │   ├── ClockEstimator.*
│   │   └── VocalTransport.*
│   ├── import/
│   │   └── UstxImporter.*
│   └── dsp/
│       └── RealtimeVoiceModulation.*
├── reference/
│   ├── OpenUtau/
│   └── OPENUTAU_COMMIT.txt
├── scripts/
│   ├── fetch_adachi_rei.*
│   ├── build_and_test.*
│   └── run_rack_e2e.*
└── tests/
    ├── fixtures/
    ├── unit/
    ├── integration/
    ├── rack_harness/
    └── e2e/
```

The exact filenames may change, but the separation of concerns may not collapse into a single large Rack module source file.

---

## 10. Real-Time Safety and Threading

### 10.1 Rack audio thread

`Module::process()` is called for each audio frame. It must perform only bounded real-time-safe work.

The audio thread may:

- read already prepared audio samples;
- advance the high-resolution playhead;
- detect clock/reset/run/trigger edges;
- apply lightweight realtime modulation and smoothing;
- write output voltage;
- update atomic status counters.

The audio thread must not:

- open or read files;
- parse JSON, YAML, `oto.ini`, or voicebank metadata;
- decode WAV files;
- phonemize lyrics;
- allocate or free heap memory;
- acquire a blocking mutex;
- wait on a condition variable;
- invoke a resampler/render process synchronously;
- perform unbounded container operations;
- log on every sample or edge.

### 10.2 Rendering workers

Use a plugin-wide shared render service or bounded worker pool rather than creating an unbounded thread per module.

Requirements:

- Each VOCAL module owns an independent render queue and output buffer.
- Voicebank metadata and decoded sample data are shared and cached across instances.
- UI edits publish immutable score snapshots or explicit change commands.
- Worker tasks are cancelable when a module is deleted or a newer score revision supersedes the old revision.
- Module destruction and Rack shutdown must not leave background threads running.
- Worker exceptions must be caught and reported to the module as an error state rather than crashing Rack.

### 10.3 Buffering

Use chunked render-ahead with overlap tails.

Recommended starting values:

- render chunk size: one beat or one bar;
- minimum ready buffer before starting playback: two beats;
- target look-ahead: four to eight beats;
- overlap/crossfade between chunks: sufficient to avoid seams.

These values may be tuned, but the module must expose:

- rendering status;
- ready/not-ready status;
- underrun count;
- last render error.

On underrun, fade to silence. Never hold and repeat the last sample as DC or block the audio thread waiting for the worker.

---

## 11. UTAU Voicebank Support

### 11.1 Required files and behavior

V1 must load a standard UTAU voicebank folder containing common combinations of:

- WAV sample files;
- one or more recursive `oto.ini` files;
- `character.txt`;
- `prefix.map` when present;
- the voicebank's own image file;
- included license/readme files.

### 11.2 `oto.ini`

Parse and use:

- WAV filename;
- alias;
- offset;
- consonant/fixed region;
- cutoff;
- preutterance;
- overlap.

Requirements:

- Resolve sample paths relative to the containing `oto.ini`.
- Recursively discover sub-bank `oto.ini` files.
- Detect missing WAV files and malformed values.
- Report duplicate aliases without crashing.
- Define and document deterministic alias-priority behavior.

### 11.3 `prefix.map`

Implement basic pitch-dependent prefix/suffix selection because the bundled singer or imported multipitch banks may require it.

### 11.4 Character metadata

Parse common `character.txt` fields, including at minimum:

- singer name;
- image path;
- sample path when present;
- author/web metadata when present.

Support at least PNG, BMP, and JPEG character images.

### 11.5 Text encodings

Japanese UTAU banks frequently use legacy encodings. V1 must correctly handle:

- UTF-8;
- UTF-8 with BOM;
- Shift-JIS/CP932 for relevant metadata, aliases, and filenames.

The loader must report ambiguous decoding instead of producing mojibake. Display an import error or encoding choice.

### 11.6 Imported banks

The bundled Adachi Rei singer is a supported V1 configuration.

Other imported UTAU banks are **experimental** in V1. The module must:

- let the user select a folder;
- validate it;
- index aliases;
- show its name and image;
- persist a stable path/reference;
- allow relinking if the folder moves;
- fail visibly and safely when unsupported.

Do not claim universal UTAU compatibility.

---

## 12. Phonemizer Design

Implement a C++ phonemizer interface conceptually similar to OpenUtau's note-plus-neighbor model, but do not attempt binary compatibility with OpenUtau's C# plugin API.

Example shape:

```cpp
class IPhonemizer {
public:
    virtual ~IPhonemizer() = default;

    virtual std::vector<PhonemeEvent> process(
        std::span<const Note> noteGroup,
        const Note* previous,
        const Note* next,
        const Voicebank& singer) const = 0;
};
```

A `PhonemeEvent` must include at least:

- requested alias;
- relative musical position;
- target note/pitch information;
- link to the source note;
- any alias fallback diagnostics.

### 12.1 Japanese Auto phonemizer

Requirements:

- Accept Japanese kana lyrics.
- Normalize hiragana and katakana where appropriate.
- Handle basic mora, digraphs, `ん`, small `っ`, and long-vowel mark `ー` sufficiently for the bundled singer.
- Use previous-vowel context to request VCV aliases when available.
- Fall back to CV aliases when VCV aliases are unavailable.
- Use start-of-phrase aliases when available.
- Handle rests and phrase boundaries.
- Respect a note's direct alias override.
- Produce diagnostics showing the selected alias and fallback path.

Conceptual lookup order for a normal note:

```text
1. explicit alias override
2. best VCV alias using previous vowel/context
3. singer-specific mapped alias/prefix/suffix
4. plain CV/kana alias
5. normalized alternate alias
6. visible missing-alias error
```

### 12.2 English and CVVC/VCCV phonemizers

Provide these additional built-in modes:

| Mode | Required behavior |
|---|---|
| English to Japanese | Bundled Adachi Rei default. Maps ordinary English pronunciation to multiple Japanese CV/VCV aliases inside a note. |
| EN X-SAMPA | General default for external English banks. Supports Delta/OpenUtau-style start, VC, CV, cluster, and ending aliases with bracketed X-SAMPA hints. |
| English VCCV | Uses Cz-style VCCV symbols and alias sequencing. |
| Japanese CVVC | Adds available VC and ending transitions to the Japanese CV base, with a CV fallback. |

The renderer must accept multiple `PhonemeEvent`s per note so words and transition conventions are not flattened into one alias.

### 12.3 Direct Alias phonemizer

Provide a second mode named **Direct Alias**.

In this mode, the lyric/alias field is used as the requested voicebank alias with only necessary singer prefix/suffix mapping.

This is the escape hatch for advanced users and unusual imported banks.

### 12.4 Future compatibility

The interface must permit future Arpasing, singer-specific dictionaries, and other phonemizers without loading OpenUtau's C# plugins in Rack.

---

## 13. Renderer

V1 exposes exactly one renderer to the user.

Suggested user-facing name:

```text
Native V1
```

The implementation may reuse or adapt appropriate MIT-licensed OpenUtau native rendering components, or use other permissively licensed DSP components. The public architecture must still present one renderer in V1.

### 13.1 Rendering pipeline

```text
VocalScore range
    -> phonemizer
    -> alias/sample lookup
    -> apply oto timing
    -> pitch and time processing
    -> envelope and crossfade
    -> authored dynamics and vibrato
    -> rendered beat-addressed chunks
    -> realtime CV modulation
    -> VOICE OUT
```

### 13.2 Required renderer behavior

The renderer must support:

- note pitch;
- note duration;
- Japanese CV and VCV sample selection;
- `oto.ini` timing;
- pitch shifting without changing requested note duration;
- time stretching without unintentionally changing note pitch;
- note-to-note crossfades;
- authored pitch curves;
- authored dynamics curves;
- authored vibrato;
- integer semitone transpose;
- arbitrary score ranges;
- rendering from an arbitrary start tick;
- deterministic rerendering for the same inputs.

### 13.3 Renderer-specific expressions

Do not expose tension, breathiness, gender, voice color, or renderer flag collections in V1.

The internal renderer interface should include a future capability system, but the V1 UI contains only the common controls specified in this document.

### 13.4 Audio format

- Internal audio processing uses floating point.
- Support Rack sample-rate changes, including at minimum 44.1 kHz, 48 kHz, and 96 kHz.
- Convert stereo source samples to mono deterministically when needed.
- Apply a DC blocker.
- Avoid hard clipping; use sensible headroom or a safety limiter.
- Output nominal Rack audio at approximately ±5 V peak.

---

## 14. VOCAL Module Panel and Controls

Use the full-workstation concept for V1, approximately 40-50 HP.

The panel must read as a native VCV Rack module rather than a generic DAW plugin.

### 14.1 Screen

The on-panel screen must show a viewport containing:

- current section name or `SONG`;
- bar/song position;
- note blocks on pitch lanes;
- Japanese lyrics under notes;
- authored pitch curve;
- authored dynamics curve;
- selected singer;
- render status;
- current transport state.

The viewport scrolls and zooms across an arbitrary-length score.

### 14.2 Physical/control parameters

Required controls:

- PLAY/PAUSE button;
- RESET button;
- LOOP toggle;
- playback range toggle: `SONG` / `SECTION`;
- selected section control;
- transpose in integer semitones;
- convenient octave down/up actions for transpose;
- internal BPM when CLOCK is unpatched;
- pitch CV attenuverter;
- dynamics CV attenuverter;
- vibrato CV attenuverter;
- form/timbre CV attenuverter;
- editor expand button;
- singer selector;
- phonemizer selector;
- render-status and underrun indicators.

Suggested parameter range:

```text
Transpose: -36 to +36 semitones, integer steps
```

No automatic harmony generation is required. Users create harmonies by duplicating modules/scores and setting transpose independently.

### 14.3 Inputs

Required inputs:

```text
CLOCK
RESET
RUN
TRIG
PITCH
DYN
VIB
FORM
SECTION
```

### 14.4 Outputs

Required outputs:

```text
VOICE OUT
END
```

`END` emits a standard trigger pulse whenever the active playback range completes, including each completion while looping.

### 14.5 Suggested Rack IDs

```cpp
enum ParamId {
    PLAY_PARAM,
    RESET_PARAM,
    LOOP_PARAM,
    RANGE_PARAM,
    BPM_PARAM,
    TRANSPOSE_PARAM,
    SECTION_PARAM,
    PITCH_ATTENUVERT_PARAM,
    DYN_ATTENUVERT_PARAM,
    VIB_ATTENUVERT_PARAM,
    FORM_ATTENUVERT_PARAM,
    PARAMS_LEN
};

enum InputId {
    CLOCK_INPUT,
    RESET_INPUT,
    RUN_INPUT,
    TRIG_INPUT,
    PITCH_INPUT,
    DYN_INPUT,
    VIB_INPUT,
    FORM_INPUT,
    SECTION_INPUT,
    INPUTS_LEN
};

enum OutputId {
    VOICE_OUTPUT,
    END_OUTPUT,
    OUTPUTS_LEN
};
```

The exact enum names may change.

---

## 15. Transport and Clock Semantics

This section is normative. Do not invent different behavior without documenting a compelling technical reason.

### 15.1 CLOCK controls speed, not audio gating

CLOCK is a musical synchronization input.

It must not directly open and close the audio output.

The module must output continuous audio between clock pulses. Clock edges update the tempo/phase estimate while a sample-resolution playhead advances continuously.

Example at 120 BPM:

```text
CLOCK:   |       |       |       |
VOICE:   ~~~~~~~~~~~~~~~~~~~~~~~~~
```

The vocal must not become a series of audio fragments separated by silence.

### 15.2 Internal versus external tempo

- If CLOCK is unpatched, use the score/module internal BPM.
- If CLOCK is patched, external clock determines vocal speed.
- The score remains authored in beats/ticks.
- At 120 BPM, one quarter-note lyric lasts approximately 0.5 seconds.
- At 60 BPM, the same quarter-note lyric lasts approximately 1.0 second.

### 15.3 PPQN

Support a configurable pulses-per-quarter-note value.

Required V1 choices:

```text
1, 2, 4, 8, 12, 16, 24, 48
```

Default:

```text
24 PPQN
```

Store PPQN per module.

### 15.4 Clock estimation

- Detect rising edges with a Schmitt trigger.
- Estimate tempo from recent edge intervals.
- Reject obvious outlier pulses.
- Advance continuously between pulses.
- Apply bounded phase correction rather than causing repeated audible jumps.
- When the clock tempo changes materially, invalidate/rerender future chunks as needed.
- V1 must support stable clocks and musical-rate tempo changes; it is not required to follow audio-rate clock FM.

### 15.5 RUN

RUN is a gate controlling whether musical time advances.

- RUN high: advance/play.
- RUN low: pause and retain the exact current musical position.
- Pausing must fade audio to silence over a short click-free ramp.
- Returning high resumes from the held position by default.
- Provide a context-menu option: `RUN rising edge: Resume` or `Restart active range`.
- If RUN is unpatched, the panel PLAY/PAUSE button controls the same state.
- If RUN is patched, it is the authoritative run state.

Stopping the clock alone must not be used as the only reliable pause signal because the module cannot distinguish a stopped low-PPQN clock from the normal interval between pulses.

### 15.6 RESET

On a rising RESET edge:

- move the playhead to the start of the active playback range;
- clear pending section transitions;
- cancel obsolete future render chunks;
- do not change the RUN gate state.

Therefore:

- if RUN is high, playback restarts immediately from the beginning;
- if RUN is low, the module remains paused at the beginning.

### 15.7 TRIG

On a rising TRIG edge:

- select/sample the active section when in SECTION range mode;
- move to the active range start;
- arm and start playback when RUN is unpatched;
- if RUN is patched, restart only when RUN is high, unless a later explicit option is added.

TRIG is the normal way to launch one-word and one-phrase module instances.

### 15.8 Active playback range

Two range modes are required.

#### SONG

The active range is the entire score.

#### SECTION

The active range is the currently selected section.

If there are no sections, SECTION mode is disabled with a useful message.

### 15.9 LOOP and one-shot

LOOP determines end-of-range behavior.

#### LOOP on

- emit END;
- return to active-range start;
- continue playing while transport is running.

#### LOOP off: one-shot

- emit END;
- return the stored playhead to active-range start;
- stop and re-arm;
- wait for a new TRIG, a new RUN rising edge, or panel PLAY action;
- do not immediately restart merely because RUN remained high after the range ended.

This behavior must work for both a single word and a complete song.

### 15.10 Section CV

Default section mapping:

```text
0 V -> section index 0
1 V -> section index 1
2 V -> section index 2
...
```

Clamp to the available section range. Support at least ten directly CV-addressable sections.

The editor may contain more sections, but V1 must visibly document how indices above the directly addressable voltage range are handled.

### 15.11 Section-change quantization

Required choices:

```text
Immediate
Next beat
Next bar
End of active section
```

Default:

```text
End of active section
```

In SECTION mode with LOOP enabled, a pending new section selected by CV must take effect at the configured quantization boundary. This allows an external sequencer or random CV source to arrange vocal sections without cutting words in the middle by default.

---

## 16. Nondestructive Modulation Semantics

Incoming CV modifies the authored performance. It does not overwrite the stored score.

Unplugging all expression CV must reproduce the authored performance.

### 16.1 Effective pitch

Conceptually:

```text
effective pitch
    = authored note
    + authored pitch curve
    + module transpose
    + external pitch CV offset
    + external vibrato addition
```

### 16.2 Transpose

- Integer semitone score-level offset.
- Applied before or during base rendering in a way that preserves quality.
- Range: -36 to +36 semitones.
- Octave controls are convenience operations on the same semitone value.

### 16.3 PITCH CV

- Nondestructive realtime or low-latency control-rate pitch deviation.
- Full positive attenuverter depth should map +5 V to approximately +12 semitones and -5 V to -12 semitones.
- Smooth parameter changes enough to avoid zipper noise while preserving intentional modulation.
- This is not the same as a 1 V/oct absolute note input; it is an offset around the authored performance.

### 16.4 DYN CV

- Nondestructive realtime dynamics offset or scale.
- Document whether the implementation uses a dB-domain or multiplicative model.
- Full attenuverter depth should provide a useful range near ±12 dB at ±5 V.
- Smooth changes and prevent runaway clipping.

### 16.5 VIB CV

- Adds to or scales the currently authored vibrato depth.
- It must not erase the authored vibrato settings.
- The active note's authored vibrato rate remains the default rate.
- If the note has no authored vibrato, the implementation may use a documented default rate so VIB CV can still produce modulation.

### 16.6 FORM CV

- Provides a simple, renderer-independent timbre/formant-style shift.
- It may be implemented as a post-render spectral/formant process.
- Exact behavior does not need to match an OpenUtau flag.
- It must be stable, bounded, click-free, and musically useful.

### 16.7 Update domains

Use realtime DSP for controls that need immediate patch response where practical:

- PITCH CV;
- DYN CV;
- VIB CV;
- FORM CV.

Use render/scheduling updates for:

- note edits;
- lyric edits;
- alias changes;
- transpose changes;
- section range changes;
- tempo-dependent future chunks.

The UI must distinguish sample-rate control from parameters applied to future render chunks.

---

## 17. Full In-Module Editor

A user must be able to create and edit a complete song without OpenUtau.

The small panel display may be a viewport, but the module must provide an expanded Rack-native editor overlay/window for serious editing.

### 17.1 Required note editing

- add notes by click/drag;
- select one or multiple notes;
- move notes in time and pitch;
- resize note start/end;
- delete notes;
- copy/paste notes;
- edit lyric text;
- edit direct alias override;
- snap to a selectable musical grid;
- horizontal and vertical scroll;
- horizontal and vertical zoom;
- undo and redo.

### 17.2 Required expression editing

- draw/edit pitch-curve points;
- draw/edit dynamics-curve points;
- edit vibrato start, depth, and rate;
- display the generated/selected voicebank alias for a note;
- display a clear missing-alias or rendering error on the affected note.

### 17.3 Required section editing

- create section from a selected time range;
- rename section;
- move section boundaries;
- delete section;
- show section markers above the timeline;
- enforce or visibly flag the V1 non-overlap rule.

### 17.4 Required score operations

- New score;
- Open/import USTX;
- clear score;
- select singer;
- select phonemizer;
- set nominal BPM;
- set time signature;
- jump to bar/tick;
- zoom to full score;
- zoom to selected section.

### 17.5 UI behavior

- Editing must never block Rack audio.
- Score changes should schedule invalidation of only affected future material where practical.
- While rendering, the editor remains responsive.
- Show low-resolution or stale-preview state explicitly if the audio is not yet updated.
- The display must distinguish authored curves from the effective CV-modulated value when practical; an overlay is preferred but not required for V1 completion.

Do not attempt to visually clone the complete OpenUtau application. Build a compact Rack-oriented editor around the required operations.

---

## 18. OpenUtau USTX Import

V1 must import a documented subset of OpenUtau `.ustx` projects.

### 18.1 Import workflow

- User chooses an `.ustx` file.
- If it contains multiple tracks/voice parts, present a track/part selector.
- Import one selected vocal track into the current VOCAL module.
- Imported data becomes an independent internal VocalScore; there is no live file link.
- The module's selected singer remains authoritative unless a resolvable compatible singer path is explicitly chosen by the user.

### 18.2 Required imported data

Import at minimum:

- note start positions;
- note durations;
- MIDI pitches;
- lyrics;
- pitch-deviation points when representable;
- vibrato start/depth/rate when representable;
- dynamics/expression data when representable;
- project nominal tempo and time signature;
- explicit phonetic hints or aliases when safely representable.

### 18.3 Unsupported data

Unsupported renderer flags or expressions must not crash import.

Collect and display a post-import report containing:

- imported items;
- ignored items;
- approximated items;
- missing singer references;
- tempo-map warning;
- missing aliases after phonemization.

### 18.4 Test fixtures

Create small deterministic `.ustx` fixtures in the test suite covering:

- a simple Japanese melody;
- pitch curve;
- vibrato;
- dynamics;
- multiple tracks requiring selection;
- unsupported expression warning;
- malformed file handling.

---

## 19. Persistence and Patch Portability

The complete authored/imported score must be saved with the Rack patch.

### 19.1 Required persisted state

Persist:

- score data;
- notes, lyrics, curves, vibrato, and sections;
- selected singer ID/path;
- selected phonemizer;
- playback range mode;
- section selection;
- loop state;
- PPQN;
- RUN rising-edge behavior;
- section-change quantization;
- transpose;
- internal BPM and time signature;
- editor zoom/scroll state where reasonable;
- all module parameters.

### 19.2 Serialization

Use a versioned schema.

The score may be stored directly in `dataToJson()` using a compact representation or in Rack patch storage with a versioned file referenced by module JSON. Whichever approach is used must satisfy:

- saving a patch creates a self-contained score;
- reopening the patch does not require the original USTX file;
- old schema versions can be migrated or fail with a useful error;
- rendered audio caches are not serialized;
- external imported voicebanks are referenced, not copied into every patch;
- bundled Adachi Rei resolves by stable built-in singer ID.

### 19.3 Missing external singer

If a patch references an external voicebank that is absent:

- load the score normally;
- show `Singer missing`;
- output silence rather than crashing;
- allow the user to relink or choose another singer.

---

## 20. SINGER PLATE Module

SINGER PLATE is a visual utility module and part of V1.

### 20.1 Required behavior

- Default singer: bundled Adachi Rei.
- Singer selection through context menu or an on-panel selector.
- Read singer name and image from the same voicebank database used by VOCAL.
- Display the image as the dominant panel element.
- Display singer name optionally.
- Save singer selection and display options in the patch.
- Reload image safely when the selected voicebank changes.
- Use a generic placeholder if the bank has no valid image.

### 20.2 Display options

Required options:

```text
Fit
Fill/Crop
Show singer name on/off
```

### 20.3 Technical constraints

- No audio DSP.
- No worker rendering.
- Image decoding occurs off the audio thread.
- Share decoded image cache across plate instances where practical.
- The module must not depend on a VOCAL instance being present.

A future "follow another VOCAL module" feature is optional and not required for V1.

---

## 21. Multiple-Instance Requirements

Multiple VOCAL instances are a normal V1 use case, not a stress-only edge case.

Requirements:

- Each instance has an independent score, transport, section state, transpose, CV inputs, output, and error state.
- Instances may use the same singer without duplicating all decoded voicebank samples in memory.
- Instances may use different singers.
- Deleting one instance must not invalidate another instance's cache.
- Clock/reset/run signals can be multed to several instances and keep them musically aligned.
- Different transposes and CV modulation must remain independent.
- A minimum reference patch with three concurrent VOCAL instances must run without crashes or audio-thread blocking.

Expected harmony workflow:

```text
VOCAL Lead:     transpose  0
VOCAL Harmony:  transpose +4
VOCAL Fifth:    transpose +7
```

No separate harmony module is required in V1.

---

## 22. Mandatory Testing Strategy

Testing is a release requirement, not optional cleanup.

Implement all four levels below.

### 22.1 Unit tests

At minimum test:

- `oto.ini` parsing;
- recursive voicebank discovery;
- CP932/Shift-JIS decoding;
- `prefix.map` lookup;
- `character.txt` parsing;
- alias lookup and deterministic fallback;
- Japanese kana normalization;
- Japanese VCV selection and CV fallback;
- Japanese CVVC transition selection;
- English-to-Japanese multi-event word mapping;
- canonical EN X-SAMPA and English VCCV alias sequences;
- Direct Alias mode;
- score serialization and migration;
- section validation;
- clock edge detection;
- PPQN tempo calculation;
- transport state machine;
- USTX import mapping;
- render-cache keys and invalidation.

### 22.2 Offline renderer integration tests

Build a command-line test program using the same core library as the Rack plugin.

Example:

```bash
vocalrack-render \
  --singer res/singers/adachi-rei \
  --score tests/fixtures/rei_phrase.json \
  --out test-artifacts/offline-rei-phrase.wav
```

Required assertions:

- output file exists;
- output is finite and non-silent;
- no NaN/Inf samples;
- expected duration is within tolerance;
- output is deterministic within documented floating-point tolerance;
- no unintentional long zero gaps occur inside sustained notes;
- transpose changes measured pitch in the expected direction;
- 60 BPM output is approximately twice the duration of 120 BPM output for the same beat-length score.

A human listening check must confirm that the bundled-singer test phrase is recognizably vocal and the intended vowels/lyrics are intelligible enough for V1.

### 22.3 Rack module harness tests

Create a test harness that instantiates the actual `VocalModule` class, calls its Rack lifecycle and `process()` methods, and drives sample-by-sample inputs.

Test at minimum:

1. CLOCK pulses never directly gate the audio.
2. A 120 BPM clock plays the score at the correct beat duration.
3. A 60 BPM clock plays it at approximately half speed.
4. RUN low pauses the musical position and produces a click-free fade to silence.
5. RUN high resumes from the held point.
6. RESET returns to the active-range start.
7. TRIG restarts the active range.
8. LOOP repeats and emits END each cycle.
9. One-shot emits END, returns to start, and remains armed without immediately retriggering while RUN is still high.
10. SECTION CV selects the expected section.
11. End-of-section quantization defers a section switch correctly.
12. PITCH, DYN, VIB, and FORM CV are nondestructive and bounded.
13. Patch serialization restores the score and settings.
14. Missing singer path produces safe silence and an error state.
15. Sample-rate changes do not crash or corrupt playback.
16. Three module instances run independently.

### 22.4 Actual VCV Rack end-to-end test

This is mandatory. A harness-only test is not sufficient.

Create a test-only companion Rack module or test plugin, compiled only for E2E testing, that can:

- output deterministic CLOCK, RUN, RESET, TRIG, and SECTION signals;
- receive VOCAL audio;
- record that audio to a WAV file without writing from the Rack audio thread;
- write a JSON result file with edge timestamps, END pulse timestamps, underruns, and test status.

Prepare an actual `.vcv` patch connecting:

```text
E2E DRIVER CLOCK  -> VOCAL CLOCK
E2E DRIVER RUN    -> VOCAL RUN
E2E DRIVER RESET  -> VOCAL RESET
E2E DRIVER TRIG   -> VOCAL TRIG
E2E DRIVER SECTION-> VOCAL SECTION
VOCAL VOICE OUT   -> E2E DRIVER AUDIO IN
VOCAL END         -> E2E DRIVER END IN
```

Automate or script:

1. build the production plugin and E2E helper;
2. install them into an isolated Rack user directory;
3. launch the real VCV Rack executable with the E2E patch;
4. wait for the result file;
5. stop Rack cleanly or terminate it after results are flushed;
6. inspect Rack's log for load errors, crashes, and assertions;
7. validate the recorded WAV and timing report;
8. save all evidence under `test-artifacts/e2e/`.

Required real-Rack scenarios:

#### Scenario A: Default first sound

- Fresh isolated Rack user directory.
- No external voicebank configured.
- Add/load VOCAL with bundled Adachi Rei.
- Play a short built-in or fixture score.
- Confirm non-silent continuous vocal output.

#### Scenario B: Clock-speed behavior

- Play the same four- or six-beat phrase at 120 BPM and 60 BPM.
- Confirm the 60 BPM completion time is approximately twice the 120 BPM completion time.
- Confirm there are no clock-period silence gaps.

#### Scenario C: Pause, resume, and reset

- Pause midway using RUN low.
- Hold for a known wall-clock interval.
- Resume and confirm the phrase continues from the held musical position.
- Reset and confirm playback returns to the range start.

#### Scenario D: One-shot and loop

- One-shot must play once, emit END, return to start, and remain stopped.
- Loop must produce at least three complete cycles and one END pulse per cycle.

#### Scenario E: Sections

- Create at least two named sections.
- Select them with SECTION CV.
- Verify end-of-section quantized switching.

#### Scenario F: Multiple instances

- Run at least two VOCAL modules simultaneously.
- Use the same score with transpose `0` and `+7`.
- Route them separately to the E2E recorder or to separate channels.
- Confirm both produce audio and remain synchronized.

#### Scenario G: Save/reload

- Save the patch.
- Close Rack.
- Reopen the patch.
- Confirm score, singer, sections, transpose, and transport settings were restored.
- Confirm playback still succeeds.

#### Scenario H: SINGER PLATE

- Load SINGER PLATE in the real patch.
- Confirm Adachi Rei image and name display on a fresh install.
- Select an imported test bank with another image and confirm the panel updates.

### 22.5 E2E evidence

Do not mark the project complete without these files:

```text
test-artifacts/e2e/rack.log
test-artifacts/e2e/results.json
test-artifacts/e2e/default-first-sound.wav
test-artifacts/e2e/clock-120.wav
test-artifacts/e2e/clock-60.wav
test-artifacts/e2e/pause-resume-reset.wav
test-artifacts/e2e/one-shot.wav
test-artifacts/e2e/loop.wav
test-artifacts/e2e/sections.wav
test-artifacts/e2e/multiple-instances.wav
test-artifacts/e2e/screenshot.png
test-artifacts/e2e/TEST_REPORT.md
```

`TEST_REPORT.md` must state:

- operating system and architecture;
- Rack version;
- plugin commit;
- OpenUtau reference commit;
- Adachi Rei archive version and hash;
- exact test commands;
- pass/fail for every scenario;
- observed underrun count;
- human listening result;
- known limitations.

No end-to-end evidence means V1 is not finished.

---

## 23. Required Reference Test Score

Create an original, copyright-safe Japanese fixture such as a short sequence using simple kana or the singer's name. Do not use a copyrighted melody.

The fixture must include:

- at least six notes;
- at least two note lengths;
- one pitch curve;
- one dynamics curve;
- vibrato on at least one sustained note;
- two named sections;
- a phrase boundary;
- aliases that exercise VCV selection and CV fallback.

Also create:

1. a one-note sustained `う` or `お` score for drone/loop testing;
2. a one-word one-shot score;
3. a two-section phrase-bank score;
4. a longer multi-bar score for scrolling, saving, and render-ahead testing.

---

## 24. Error Handling and Diagnostics

The module must never crash Rack because of bad user data.

Required visible states:

```text
Ready
Rendering
Waiting for buffer
Singer missing
Voicebank invalid
Alias missing
Import warning
Render error
Buffer underrun
```

Requirements:

- Errors include actionable detail in the editor or context menu.
- Repeated sample-level errors are rate-limited.
- Missing alias diagnostics identify the lyric, note, and aliases attempted.
- Invalid USTX imports leave the current score unchanged.
- A render failure for one chunk does not destroy the whole module state.
- The user can retry rendering after correcting a path or alias.

---

## 25. Performance and Stability Targets

These are V1 targets and may be tuned based on measured results.

- The Rack audio thread performs no blocking operation.
- After initial pre-roll, the standard E2E test has zero buffer underruns.
- Three moderate VOCAL modules can play for five minutes at 48 kHz without a crash, deadlock, or unbounded memory growth.
- Repeated start/stop/reset for 1,000 cycles does not leak workers or buffers.
- Loading and deleting VOCAL 100 times does not leave background jobs alive.
- A malformed voicebank containing missing samples and malformed `oto.ini` lines fails safely.
- Closing Rack during rendering exits cleanly.

Add debug counters for:

- active render jobs;
- canceled jobs;
- cache hits/misses;
- decoded sample memory;
- ring-buffer fill level;
- underruns;
- render duration by chunk.

These counters may be hidden behind a developer/debug menu in release builds.

---

## 26. Build, CI, and Packaging

### 26.1 Build

Support the ordinary Rack plugin flow:

```bash
export RACK_DIR=/path/to/Rack-SDK
make
make dist
```

### 26.2 CI

At minimum CI must:

- build the standalone core/tests;
- run unit tests;
- run offline integration tests using test fixtures;
- build the Rack plugin;
- verify the distribution contains Adachi Rei assets and third-party notices;
- fail on missing licenses or required assets.

Build for these targets when practical:

- macOS arm64;
- macOS x64;
- Windows x64;
- Linux x64.

The actual Rack GUI E2E test may run on one designated reference platform, but it must be rerunnable through a documented script.

### 26.3 Release package

The `.vcvplugin` package must contain:

- VOCAL module;
- SINGER PLATE module;
- bundled Adachi Rei singer files required for playback;
- voicebank terms/attribution;
- MIT license;
- third-party notices;
- a concise manual;
- at least one demo patch or native demo score.

A new user must not need OpenUtau installed to run the plugin.

---

## 27. Documentation Requirements

Create:

```text
README.md
MANUAL.md
ARCHITECTURE.md
THIRD_PARTY_NOTICES.md
IMPLEMENTATION_STATUS.md
test-artifacts/e2e/TEST_REPORT.md
```

### README.md

Must explain:

- what VocalRack is;
- that it is Vocaloid/UTAU-style singing synthesis inside Rack without a DAW;
- installation;
- first-sound steps;
- bundled Adachi Rei singer;
- importing a voicebank;
- creating a score;
- importing USTX;
- basic clock/run/reset patching;
- licensing distinction between code and singer assets.

### MANUAL.md

Must document every control, input, output, transport mode, CV mapping, PPQN choice, section behavior, and error state.

### ARCHITECTURE.md

Must document:

- thread boundaries;
- score model;
- voicebank model;
- phonemizer interface;
- renderer interface;
- render cache;
- transport state machine;
- serialization format;
- extension points for future phonemizers/renderers/modules.

### IMPLEMENTATION_STATUS.md

Track every numbered V1 requirement and its completion/test status. No required item may remain marked TODO when V1 is declared finished.

---

## 28. Suggested Implementation Order

This is an implementation order, not a set of stopping points.

### Phase 1: Repository and legal/provenance setup

- initialize Rack plugin;
- add MIT license;
- clone and pin OpenUtau reference;
- download, hash, inspect, and vendor Adachi Rei;
- create third-party notices.

### Phase 2: Standalone core model

- VocalScore, Note, Curve, Vibrato, Section;
- serialization;
- unit tests.

### Phase 3: Voicebank loader

- encodings;
- recursive `oto.ini`;
- prefix map;
- character metadata/image;
- Adachi Rei validation.

### Phase 4: Phonemizer

- Direct Alias;
- Japanese Auto;
- Japanese CVVC;
- English to Japanese;
- EN X-SAMPA;
- English VCCV;
- alias diagnostics;
- tests against Adachi Rei.

### Phase 5: Offline first sound

- render a hardcoded/fixture phrase to WAV;
- correct oto timing, pitch, stretch, and crossfades;
- human-listen and iterate until intelligible.

### Phase 6: Render service

- chunking;
- immutable score snapshots;
- cache;
- cancellation;
- ring buffers;
- shared voicebank cache.

### Phase 7: Minimal Rack module

- VOICE OUT;
- internal BPM;
- play/pause/reset;
- bundled singer;
- actual Rack smoke test.

### Phase 8: Clock and transport

- CLOCK/PPQN;
- RUN;
- RESET;
- TRIG;
- one-shot;
- loop;
- END output;
- transport tests.

### Phase 9: Modulation and transpose

- semitone/octave transpose;
- PITCH/DYN/VIB/FORM inputs and attenuverters;
- realtime safety and smoothing;
- multiple-instance tests.

### Phase 10: Editor and sections

- full note editor;
- curves and vibrato;
- section editing;
- section CV and quantization;
- save/reload.

### Phase 11: USTX import

- documented subset;
- warnings;
- test fixtures.

### Phase 12: SINGER PLATE

- image/name display;
- bank selection;
- persistence;
- real Rack visual test.

### Phase 13: Mandatory E2E and release hardening

- build E2E driver and patch;
- run every scenario;
- fix failures;
- save WAV/log/screenshot/report artifacts;
- package distributable plugin;
- complete documentation.

Do not stop after any intermediate phase.

---

## 29. Definition of Done

V1 is finished only when all of the following are true:

- [ ] The project builds as a VCV Rack 2 plugin.
- [ ] Project-authored code is MIT licensed.
- [ ] The Rack plugin is distributed free of charge.
- [ ] OpenUtau has been cloned and its exact reference commit recorded.
- [ ] The official Adachi Rei UTAU voicebank has been downloaded, hashed, reviewed, attributed, and included in the package under its own terms.
- [ ] A fresh install produces first sound with Adachi Rei and no external singer setup.
- [ ] VOCAL can contain and edit an arbitrary-length score.
- [ ] A complete song can be authored inside the module.
- [ ] A supported OpenUtau USTX track can be imported.
- [ ] Japanese Auto, Japanese CVVC, English to Japanese, EN X-SAMPA, English VCCV, and Direct Alias phonemizers work.
- [ ] Bundled Adachi Rei sings ordinary English lyrics through English to Japanese without missing aliases.
- [ ] Matching external EN X-SAMPA and English VCCV banks render canonical OpenUtau alias sequences.
- [ ] The bundled singer produces intelligible Japanese singing.
- [ ] CLOCK controls vocal speed without gating audio.
- [ ] Internal BPM works when CLOCK is unpatched.
- [ ] RUN pauses/resumes deterministically.
- [ ] RESET returns to the active-range beginning.
- [ ] TRIG starts/restarts the active range.
- [ ] LOOP and one-shot behavior match this specification.
- [ ] Sections can be created, selected, looped, and changed by CV with quantization.
- [ ] Transpose works in integer semitones with octave convenience actions.
- [ ] PITCH, DYN, VIB, and FORM CV nondestructively modify the authored performance.
- [ ] VOICE OUT provides continuous mono Rack-level audio.
- [ ] END emits at each active-range completion.
- [ ] Multiple VOCAL modules work concurrently and independently.
- [ ] SINGER PLATE displays Adachi Rei by default and imported singer images when selected.
- [ ] Saving and reopening a patch restores the complete score and state.
- [ ] Missing/corrupt external data fails safely.
- [ ] No file I/O, blocking mutex, or expensive synthesis occurs in `process()`.
- [ ] Unit tests pass.
- [ ] Offline integration tests pass.
- [ ] Rack module harness tests pass.
- [ ] The real VCV Rack E2E scenarios pass.
- [ ] All required E2E WAV, log, JSON, screenshot, and report artifacts exist.
- [ ] A human listening check is recorded in `TEST_REPORT.md`.
- [ ] `IMPLEMENTATION_STATUS.md` contains no unresolved required TODOs.
- [ ] `make dist` produces an installable `.vcvplugin` containing the modules, default singer, notices, and documentation.

---

## 30. Authoritative External References

Use these official references during implementation:

```text
VCV Rack plugin tutorial:
https://vcvrack.com/manual/PluginDevelopmentTutorial

VCV Rack plugin API guide:
https://vcvrack.com/manual/PluginGuide

VCV Rack plugin licensing:
https://vcvrack.com/manual/PluginLicensing

OpenUtau repository:
https://github.com/openutau/OpenUtau

OpenUtau phonemizer API reference:
https://github.com/openutau/OpenUtau/blob/master/OpenUtau.Core/Api/README.md

Official Adachi Rei page and UTAU downloads:
https://mechanicalgirl.jp/adachi-rei/

Official Adachi Rei character/voice guidelines:
https://mechanicalgirl.jp/guidelines/
```

For ambiguous behavior, inspect the pinned OpenUtau source and document the decision in `ARCHITECTURE.md`.
