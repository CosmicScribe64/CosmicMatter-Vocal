# VocalRack Architecture

## Reference versions

- VCV Rack / SDK: **2.6.6**, macOS ARM64 reference build. SDK archive SHA-256: `29414e52417992cbafa47e30f947c3c0c7a34e5c424bb83c5a0af8c24840481f`; CI uses a matching Rack 2 SDK per target.
- OpenUtau reference: `8bef4418feb253d06e8b39956298367294afcf06` in `reference/OPENUTAU_COMMIT.txt`.
- Adachi Rei standard UTAU bank: 3.5.0, archive SHA-256 `b96d1b21145f22e573afd9ec8aeaad0ec9cbaee581c2623c64addeb31de46b3d`.

OpenUtau is a development reference only. No OpenUtau source file is copied into the V1 runtime.

## Layers

`src/core` owns the tick-based score model and versioned serialization. `src/voicebank` owns text decoding, metadata, recursive oto/prefix indexing, and deterministic alias lookup. `src/phonemizer` maps notes plus neighbor context to phoneme events. `src/render` owns WAV I/O, Native V1 synthesis, the shared worker pool, chunk assembly, cache, and real-time publication. `src/transport` owns clock estimation and the transport state machine. `src/dsp` owns realtime CV modulation. `src/import` owns the documented USTX, legacy UST and Standard MIDI score adapters. `src/rack` is the thin Rack module/editor/panel layer.

The standalone renderer and all non-UI tests link the same core `.cpp` sources as the plugin.

## Score model and serialization

`VocalScore` uses signed 64-bit ticks at 480 PPQN. Notes contain an authored MIDI pitch, lyric/direct alias, piecewise-linear pitch/dynamics curves, OpenUtau-compatible `pitchSnapFirst`, vibrato, and optional phoneme-timing overrides. A connected note without an authored pitch curve uses OpenUtau's fresh-install Standard 80 ms sine-eased portamento when it exactly touches its predecessor. With an authored curve, `pitchSnapFirst` replaces only the first point's relative tone, matching `UNote.Validate`; disabling it leaves the whole curve authored. A rest always disconnects the notes. The shared absolute-timeline pitch helper lets incoming negative-time points begin before the boundary and is used by the renderer and piano-roll overlay, while USTX import/export preserves `snap_first` and the equivalent points. Timing overrides comprise musical position plus preutterance, overlap, attack, and release deltas; empty values inherit the selected singer's `oto.ini`. Sections are ordered non-overlapping ranges. `normalize()` supplies IDs, deterministic ordering, finite/clamped timing, and `validate()` rejects negative/overlapping data.

Schema 2 is JSON embedded as a string inside Rack module `data`. This keeps a patch self-contained without serializing caches. The serializer migrates the original schema-1 position/duration/tone representation. Stable bundled singer IDs avoid machine-specific paths.

## Rack persistence and project interchange

Rack patch data stores the schema-2 score, note-local phoneme timing, singer reference, phonemizer, clock PPQN, rising-RUN behavior, section quantization, panel transport state, and editor view and snap settings. Rack persists ordinary parameters such as loop, range, BPM, transpose, section index, and CV attenuverters through its parameter system. Rendered audio and decoded sample caches are rebuilt and never enter the patch.

The `.vocalrack` project format captures the same musical and module state for transfer between module instances or Rack patches. External singer paths remain unresolved after restore until the user relinks the folder. This avoids reading an unconfirmed machine-specific path. Invalid score data falls back to the default score and records a visible error; schema-1 scores migrate to schema 2 and inherit voicebank timing when they have no timing overrides.

USTX export maps compatible score semantics for use in OpenUtau. Rack cables, CV routing, transport latches, playback sections, editor state, and VocalRack attack/release deltas do not have USTX equivalents. A `.vocalrack` file is therefore the lossless interchange format for VocalRack, while USTX is the DAW/editor interchange format.

## Voicebank and phonemizer model

Text decoding recognizes UTF-8/BOM, UTF-16LE, and CP932 via `iconv`. Recursive `oto.ini` paths are sorted; the first path/line for a duplicate alias wins and diagnostics retain duplicates/malformed/missing files. Prefix/suffix mapping is tone-dependent. Character YAML portrait metadata is preferred when valid, followed by `character.txt`.

`IPhonemizer` does not depend on OpenUtau's C# ABI. Each implementation receives the current note, its neighbors, and a singer. It returns one or more `PhonemeEvent`s with the requested and selected aliases, relative tick, source note, target pitch, attempted fallbacks, and resolved `OtoEntry`. Implementations cover English-to-Japanese, EN X-SAMPA, English VCCV, Japanese CV/VCV auto, Japanese CVVC, and Direct Alias. Multi-event English words use the same envelope and handoff renderer as one-event Japanese notes.

## Native V1 renderer

The renderer phonemizes a requested tick range, resolves official source WAVs, applies oto offset/consonant/cutoff/preutterance timing plus any note-local phoneme timing deltas, and obtains source F0 from standard UTAU `.frq` metadata with a YIN-style fallback. It preserves the fixed consonant region and pitch-shifts the stable vowel with deterministic pitch-synchronous overlap/add, which preserves the recorded formant envelope instead of raising it with playback speed. It then applies authored curves/vibrato/dynamics, overlap-aware attack/release fades, a DC blocker, and a soft limiter. Empty timing overrides take the original path, so existing scores retain their sound. The same inputs yield byte-identical floating-point output on the reference toolchain.

The render service partitions an active range into one-bar beat-addressed chunks with overlap tails. Each chunk is synthesized on one of at most two plugin-wide workers, cropped to its core range, and assembled into an immutable ready buffer. Per-phoneme work buffers cover only the requested slice plus grain context, while epochs remain anchored to the event origin. The assembled mono buffer is capped at 128 MiB. Playback accepts only the publication serial for the current request; stale lyrics, singer, tempo, or transpose renders fade to silence. Cache keys include score hash/revision, singer content revision, sample rate, BPM, transpose, range, and phonemizer.

Decoded singer samples and rendered ranges each have a 128 MiB process-wide cap. Sample keys contain file size and modification time; render keys contain a voicebank manifest revision. Each module owns an independent `RenderSlot`, and every submitted generation owns a separate cancellation token. A replacement cancels only its predecessor; obsolete or partially canceled generations cannot publish audio, errors, or rendering state for a newer request. A real-time hazard pointer protects the current raw audio buffer while workers retire replaced owners; obsolete owners are reclaimed on publication instead of accumulating for module lifetime. Score snapshots use the same single-reader hazard pattern.

## Thread boundaries

Rack audio `process()` performs only fixed-cost edge detection, tick arithmetic, atomic snapshot publication reads, immutable sample interpolation, short delay-line modulation, fades, lights, and output writes. It performs no file I/O, decoding, parsing, heap allocation/free, mutex acquisition, condition-variable wait, or synthesis.

The Rack UI thread publishes immutable score snapshots, submits jobs, handles editor operations/file dialogs, copies diagnostics, and decodes portraits through Rack's cache. Bounded asynchronous loaders discover and validate voicebanks; restored external paths remain unresolved until explicit user relinking. Render workers do phonemization, WAV decode/cache lookup, chunk synthesis, and generation-owned error capture. The `RenderService` pool is bounded to two workers and joins during shutdown.

The audio reader uses this sequence: load current pointer, publish hazard, recheck current, read immutable data, clear hazard. A worker moves the old shared owner to a retired list, publishes the new pointer, and reclaims every retired owner not protected by the hazard. This makes publication lock-free to the audio thread and bounded under repeated edits.

## Transport state machine

The playhead tracks musical time independently of rendered samples. Internal BPM or the external Schmitt/median estimator advances it each sample. Vocal rejects one half/double-tempo interval; three consistent outliers rebase the tempo. The estimator remains warm through reset and armed restart. Material BPM or transpose changes request new future render material through an atomic flag.

RUN chooses advance/hold, RESET rewinds without changing RUN, TRIG restarts subject to RUN authority, and one-shot uses an `armed` state so a held RUN does not retrigger. Range completion emits one logical END event before loop/re-arm. Pending section indices apply at immediate, beat, bar, or end-of-section boundaries.

## Realtime modulation

`RealtimeVoiceModulation` uses fixed preallocated delay lines and smoothed controls. Pitch CV maps full ±5 V to ±12 semitones through dual-head delay resampling. DYN is ±12 dB. VIB adds bounded default-rate modulation. FORM is a stable spectral tilt. A final DC blocker and limiter keep output finite and within Rack level.

## Score import

The importer selects the format by file signature rather than trusting the extension. All paths build a temporary result and throw on malformed or unusable structure, so callers replace the active score only after success.

The USTX path is a narrow deterministic YAML-subset reader shaped by the pinned OpenUtau serializer. It enumerates nonempty tracks and voice parts, then maps note position, duration, tone, lyric, representable pitch points, dynamics curves, vibrato, the first tempo and meter, safe phonetic hints, and first-phoneme timing overrides. Timing overrides are stored as deltas from the active voicebank. Sparse part-wide pitch, deviation, and dynamics curves are interpolated across note boundaries before becoming self-contained note curves. The import report records unsupported expressions, extra generated phonemes, renderer settings, and global tempo or meter changes.

The legacy UST adapter recognizes UTF-8, UTF-16LE, and CP932/Shift-JIS text. It maps notes, rests, tempo, Mode2 pitch, intensity, vibrato, and supported preutterance and envelope values. A legacy `Lyric` field normally contains a selected voicebank alias, so non-continuation lyrics become direct aliases. Renderer flags and fields without a VocalRack equivalent remain in the import report.

The Standard MIDI adapter parses PPQN formats 0 and 1, filters the file to nonempty monophonic melody tracks, and maps note timing and pitch plus lyric/text events at note onsets. It adopts the first tempo and meter. Notes without lyrics receive a visible neutral `a`; polyphonic chord and drum tracks are not offered for import.

Imported files never select runtime code. Singer, phonemizer, renderer, resampler, wavtool, flags, voice colors and unknown expressions are treated as data to report, not plugins to execute. This keeps project import deterministic and prevents a downloaded song from expanding the plugin's runtime dependency or trust boundary.

## OpenUtau compatibility and regression

`make openutau-compare` renders one six-note USTX from the same Adachi Rei source material in three paths: A is VocalRack, B is pinned OpenUtau WORLDLINE-R, and C is pinned OpenUtau Classic with the built-in `worldline` resampler and `convergence` wavtool. The comparison uses fresh-install Standard portamento and no authored vibrato. Results are written to `test-artifacts/openutau-ab/`. OpenUtau runs in the development container and is not a runtime dependency.

`make openutau-roundtrip` exports a score through the production USTX writer, loads and renders it in pinned OpenUtau Classic, then imports and renders that generated file in VocalRack. This checks both interchange directions against the real application.

`make openutau-regression` and `make openutau-english-regression` are release gates. They measure ordered phone selection and timing, internal consonants, phoneme boundaries, sustained pitch, per-phone contribution, level, spectral texture, and stability against pinned OpenUtau Classic. The suites cover Japanese, Adachi-accented English, EN X-SAMPA, and English VCCV. The Adachi English corpus contains 119 notes in 25 phrases. Machine-readable reports, plots, and listening WAVs are stored under `test-artifacts/`.

## SINGER PLATE and UI

VOCAL's compact panel is a viewport; the expanded overlay owns full editing. Its aligned lower lanes separate phoneme timing, pitch cents, and dynamics dB. The phoneme lane derives the inherited envelope from the active `OtoEntry`, renders selected START/XFADE handles with large hit areas, and commits drags as undoable score changes. UI edits operate on the mutable UI score, then publish a new immutable snapshot/revision. The decorative plate contains no ports or DSP and queries the same voicebank metadata model. Rack's `loadImage` cache supplies shared PNG/BMP/JPEG decoding.

## Extension points

New phonemizers implement `IPhonemizer`. New renderers can consume `VocalScore`, `Voicebank`, and `RenderOptions`, while different chunk schedulers can retain the `RenderSlot` publication contract. Visual and control modules can use the core without depending on a VOCAL widget. V1 defers renderer-specific expression capability negotiation.
