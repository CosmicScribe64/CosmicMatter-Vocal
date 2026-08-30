# VocalRack V1 Test Report

Final validation date: 2026-08-27

## Build and provenance

| Item | Recorded value |
|---|---|
| Reference OS | macOS 26.6.2 (build 25G83), ARM64 |
| VCV Rack | VCV Rack Free 2.6.6 macOS ARM64 |
| Rack SDK | 2.6.6; archive SHA-256 `29414e52417992cbafa47e30f947c3c0c7a34e5c424bb83c5a0af8c24840481f` |
| Native compiler | Apple clang 21.0.0, target `arm64-apple-darwin25.6.0` |
| Docker | Docker 29.7.2; Ubuntu 24.04 core/offline image and .NET 8 OpenUtau comparison image |
| Plugin version | 2.0.0 (Rack ABI major 2; product V1) |
| Plugin source | The repository `main` commit used for a build plus the external package SHA-256 form its reproducible identity. The exact commit is supplied when a VCV Library update is requested. |
| OpenUtau reference | `8bef4418feb253d06e8b39956298367294afcf06` |
| OpenUtau A/B renderer | WORLDLINE-R from the pinned source tree |
| Bundled singer | Official 足立レイ / Adachi Rei standard UTAU bank 3.5.0 |
| Singer archive SHA-256 | `b96d1b21145f22e573afd9ec8aeaad0ec9cbaee581c2623c64addeb31de46b3d` |
| Final package | `dist/CosmicMatter-2.0.0-mac-arm64.vcvplugin` |
| Final package SHA-256 | Recorded after packaging in `dist/SHA256SUMS`; a package cannot embed its own final hash without changing that hash. |

The official singer archive, exact download URL, date, shipped files, original terms, and extraction method are recorded in `third_party/adachi_rei/SOURCE.md`. VocalRack code is MIT; the singer recordings and character assets retain the original Mechanical Girl terms.

## Exact validation commands

```sh
make -f tests/Makefile tests
make -f tests/Makefile offline
make -f tests/Makefile rack-harness
make -f tests/Makefile stress
make docker-test
make openutau-compare
make openutau-regression
make openutau-english-regression
make -j2
make release
VCVPLUGIN_PATH=dist/CosmicMatter-2.0.0-mac-arm64.vcvplugin sh scripts/validate_release.sh
python3 scripts/validate_e2e.py test-artifacts/e2e
```

The actual Rack automation is reproducible with:

```sh
RACK_BIN="/Applications/VCV Rack 2 Free.app/Contents/MacOS/Rack" sh scripts/run_rack_e2e.sh
```

That script builds both packages, validates the production archive, installs them in an isolated Rack user directory, opens the generated `.vcv` patch in the real Rack executable, waits for sample-by-sample results, captures evidence, closes Rack, opens the saved patch in a fresh Rack process, and validates the reload results. Previous canonical evidence is moved into that run's timestamped runtime directory so stale completion markers cannot pass a rerun.

## Automated test levels

| Level | Result | Coverage/evidence |
|---|---|---|
| Unit | PASS | Ten groups: voicebank/oto/encoding/prefix/metadata, six phonemizer modes/fallback, score/schema/sections, USTX/UST/MIDI mapping and malformed input, clock/transport/PPQN/phase, cache/service, realtime modulation, renderer scheduling/replacement, editor navigation, the shared draw/move/group-move/left-resize/right-resize overwrite operation, and the shared clamped internal-phoneme-divider operation used by the live Rack widget. |
| Official-bank offline integration | PASS | Eleven groups: official 721-WAV bank load, non-silent finite deterministic render, duration and internal-gap limits, pitch direction, 60/120 BPM ratio, 44.1/48/96 kHz, arbitrary seek/range, malformed-bank safety, and the English word/phone contribution matrix. |
| Actual Rack ABI harness | PASS | Seven normal groups plus the eight-group stress run, containing the required lifecycle/process/ports/serialization/sample-rate/missing-singer/multi-instance behaviors, editor audition decay, and linked native-file-dialog filter parsing. |
| Stress/stability | PASS | Three modules for a simulated five minutes, 1,000 resets, 100 create/delete cycles, repeated rerender/cancel/reclamation, malformed data, and worker shutdown. |
| Docker clean build/test | PASS | Ubuntu 24.04 standalone core and official-bank offline suites; no compiler or .NET SDK was installed on the host for these portable tests. |
| Real-world import probe | PASS | Production importer accepted a 436 KiB community OpenUtau song with four nonempty parts and 903 notes total (plus the 16-note legacy UST fixture); empty/non-vocal tracks were omitted. See `test-artifacts/import-corpus/TEST_REPORT.md`. |
| Actual VCV Rack 2.6.6 | PASS | Production `.vcvplugin` plus test helper loaded in Rack; scenarios A-K, six concurrent VOCAL instances, external ADSR/VCA, stock reverb, complete song, zero underruns, and fresh-process reload all passed. |
| Same-USTX OpenUtau A/B/C | PASS | VocalRack Native V1, pinned OpenUtau WORLDLINE-R, and pinned OpenUtau Classic rendered the same neutral `tests/fixtures/adachi_rei_ab.ustx` with the same Adachi Rei bank in Docker. The comprehensive Japanese Classic gate covers 628/625 notes, 543 boundaries, all available VCV aliases, all 361 articulation joins, duration/register stress and independent expression contours. The English Classic gate aligns all 264/264 Adachi reference phones with 100% recall/precision and 0 ms p90 timing; separate X-SAMPA and VCCV legs align 16/16 phones each. |

Offline artifacts include `offline-rei-phrase.wav`, `offline-rei-phrase-60.wav`, `offline-rei-phrase-plus7.wav`, and the user-reviewed `revised-rei-phrase.wav` under `test-artifacts/offline/`.

## Actual Rack scenario results

The production package, rather than a test-linked substitute, was loaded for all VOCAL and SINGER PLATE modules. `results.json` has `passed: true`; every named check is true.

| Scenario | Result | Observed evidence |
|---|---|---|
| A: default first sound | PASS | Bundled Adachi Rei rendered without external setup; RMS 0.1297, peak 0.3599, END at frame 165376. |
| B: clock speed | PASS | END frame 165744 at 120 BPM and 330382 at 60 BPM: ratio 1.99333. Vocal audio remained present between clock edges; validator continuity and lead-in gates passed. |
| C: pause/resume/reset | PASS | Known RUN-low hold produced silence, resume continued from the held musical point, and RESET returned to range start; output RMS 0.1225. |
| D: one-shot | PASS | Exactly one END pulse at frame 165193; transport re-armed without retriggering while RUN stayed high. |
| D: loop | PASS | Three cycles completed with END at frames 165560, 330935, and 496310; longest inter-cycle quiet interval was 20.2 ms. |
| E: sections | PASS | Requested section indices `[1, 0, 1]` followed SECTION CV/end-of-section quantization; six completions were observed at frames 44284, 88384, 132484, 176584, 220684, and 308884. |
| F: multiple instances | PASS | Independent +0 and +7 outputs remained synchronized; recorded secondary energy 117443.099. Four additional VOCAL instances exercised section, word, song, and vowel states at the same time. |
| G: save/reload | PASS | Rack saved the patch, closed, and reopened it in a fresh application process. Reload RMS 0.1297, one END, zero underruns; all six score hashes/singers/sections/transposes/transports matched. |
| H: SINGER PLATE | PASS | Screenshot shows the bundled Adachi Rei plate and a second plate loaded from persisted external-bank selection with a distinct generated image and name. |
| I: triggered word | PASS | TRIG launched the one-word SECTION range; one END arrived at frame 43733, output RMS was 0.0731, and it re-armed without retriggering. |
| J: vowel instrument/CV | PASS | A sustained `う` remained continuous. Realtime modulation changed RMS from 0.2374 to 0.2539 without changing the stored score. |
| J: external ADSR/VCA | PASS | A stock Fundamental ADSR/VCA shaped the Rack signal path: active RMS 0.1960, released RMS 0.00335. Vocal leaves this function to Rack modules. |
| J: stock reverb | PASS | Valley Plateau produced a wet tail: RMS 0.1436 at 5.7-7.0 s versus 0.0230 on the dry shaped path. |
| K: complete song | PASS | The multi-bar score reached END at frame 705234 with no pre-END silence gap above 120 ms, truncation, or underrun. |
| K: song through reverb | PASS | The complete wet capture differed from dry and retained a 0.1060 RMS tail from 16.2-18.5 s while the dry tail was silent. |

Observed module underrun counters were `[0, 0, 0, 0, 0, 0]`; the reload underrun count was `0`. No plugin load error, assertion failure, segmentation fault, or fatal error was found in `rack.log`.

Required evidence present in this directory:

- `rack.log`
- `results.json` and `results.raw.json`
- `default-first-sound.wav`
- `clock-120.wav` and `clock-60.wav`
- `pause-resume-reset.wav`
- `one-shot.wav` and `loop.wav`
- `sections.wav`
- `multiple-instances.wav`
- `triggered-word.wav`
- `sustained-vowel-baseline.wav`, `sustained-vowel-modulated.wav`, `sustained-vowel-shaped.wav`, and `sustained-vowel-reverb.wav`
- `full-song-dry.wav` and `full-song-reverb.wav`
- `screenshot.png`
- `screenshot-provenance.json`
- `editor-overview.jpeg`, `editor-phoneme-timing.png`, and `editor-tooltip.png`
- `saved-and-reloaded.vcv`, `reload-results.json`, and `reload-sound.wav`

The installed-plugin pointer/edit/import journey and all 41 user stories were also audited during release qualification. Their working notes are intentionally kept out of the public plugin package; the reproducible shipped gates are summarized here.

The current editor implementation calls `placeEditorDrawnNote()` and `applyEditorNoteGesture()` directly from its Rack pencil, context-menu, drag, and resize paths. The unit gate exercises those same operations for note creation into an occupied span, snapped body movement, rigid multi-note movement, left-edge resize, and right-edge resize. In every case the edited note or selected group wins and collided material is trimmed, rebased, or removed without leaving a polyphonic score. This closes the former gap where only the lower-level collision resolver was tested.

Internal white phoneme dividers call `setInternalPhonemeBoundaryTick()` directly from the Rack drag path. The same test gate proves independent movement, neighbour/end clamping, native JSON persistence, and OpenUtau USTX indexed-offset round-trip. The official Adachi Rei offline gate then moves only `star`'s medial `た` event, proves the surrounding `す` and `う` ticks remain unchanged, and requires a measurable rendered-audio difference.

## Human listening result: PASS

A machine-passing candidate failed the listening check because it sounded high-pitched and was hard to understand, although the listener recognized a Vocaloid-like voice. Its evidence remains under `test-artifacts/e2e-initial-listening-failed/` as a failed run.

The renderer now schedules from `oto.ini` preutterance, preserves the consonant and fixed region before shifting the vowel body, applies overlap-aware attacks and releases, and uses a lower default register. A later same-USTX A/B exposed a source-pitch fault in the first revision: the first four VocalRack notes measured about +1900, +1900, +1220, and +1200 cents. The listener described that discarded render as "so high pitched and kinda ear hurting."

The root cause was an autocorrelation estimator selecting two- and three-period subharmonics for Adachi Rei's source samples. VocalRack now reads the official bank's supplied UTAU `.frq` average pitches (with a YIN-style fallback for banks without metadata). The corrected comparison measures all six fixture notes within approximately 11 cents of their targets, lowers the formerly harsh centroid, and restores low/mid warmth.

The listener recognized the intended Adachi Rei phrase and described the same-score VocalRack/OpenUtau Classic A/C result as "quite a bit closer," then "really really close" and "sooo close." They heard Classic as warmer, fuzzier, and sharper at some consonant transitions; VocalRack sounded smoother and less buzzy. This passes V1 listening acceptance: intelligible Adachi Rei singing that is close to Classic without claiming phase identity or copying Classic's residual buzz. The real-Rack WAVs in this directory use the corrected renderer.

## VocalRack/OpenUtau same-source comparison

`make openutau-compare` uses the one six-note `adachi_rei_ab.ustx` source, the same Adachi Rei bank, 120 BPM, and 44.1 kHz for both engines. OpenUtau and all NuGet/build products remain in an ephemeral Docker container. The output directory is `test-artifacts/openutau-ab/`.

| Metric | VocalRack | OpenUtau WORLDLINE-R |
|---|---:|---:|
| Duration | 3.500000 s | 3.499252 s |
| Peak | 0.465546 | 0.551636 |
| RMS | 0.144814 | 0.163678 |
| First measured sound | 0.001406 s | 0.000000 s |
| Active duration | 3.498571 s | 3.499252 s |

The best 10 ms RMS-envelope alignment had 0.000 seconds lag and correlation 0.7460. This compares timing and phrase structure; the two synthesis algorithms produce different waveforms. `ab-sequential.wav` plays VocalRack then OpenUtau. `ab-aligned-stereo.wav` places VocalRack left and OpenUtau right. The two log files record phoneme and renderer provenance, and `comparison.json` contains the metrics.

## Known V1 limitations

- Built-in modes are Japanese Auto CV/VCV, Japanese CVVC, English-to-Japanese, English X-SAMPA, English VCCV, and Direct Alias. Vocal loads compatible third-party UTAU banks, but does not execute third-party OpenUtau renderer or phonemizer plug-ins; unavailable project settings are reported during import.
- USTX import uses the first tempo and time signature, then reports later changes. Vocal reports renderer-specific expressions without applying them.
- Native V1 is not intended to null against or exactly imitate WORLDLINE-R.
- One VOCAL instance is mono and monophonic. Editor draw, move, paste, and resize gestures resolve collisions deterministically: the edited note or rigid selection wins, while collided unselected material is trimmed/rebased or removed.
- SINGER PLATE external-bank verification in the automated Rack patch loads a genuine external selection from persisted state; the screenshot proves the updated image/name, but the automation does not synthesize a mouse click through Rack's folder dialog.
- The provided GUI E2E orchestration script targets the designated macOS ARM64 Rack 2.6.6 reference platform. Core/offline tests are portable through Docker and CI describes other targets where matching Rack SDKs are available.
- Published builds are identified by both the immutable source commit and the package SHA-256 recorded outside the archive.

These limitations match the explicit V1 scope and do not leave a required acceptance item unresolved.
