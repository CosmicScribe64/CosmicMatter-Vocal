# VocalRack V1 Implementation Status

Status date: 2026-08-27. Reference platform: macOS 26.6.2 ARM64, VCV Rack Free 2.6.6. Product V1 uses Rack ABI-compatible manifest version 2.0.0.

## Numbered specification audit

| Spec | Status | Implementation / evidence |
|---|---|---|
| 1-3 Directive, product, goals | Complete | Native Rack modules, shared engine, default singer, editing/import, continuous clocked audio, independent instances, persistence, and Rack evidence. |
| 4 Initial setup | Complete | Rack SDK 2.6.6; pinned OpenUtau clone; official unchanged Adachi Rei 3.5.0 archive, SHA-256 and extraction provenance; release validation hard-fails when singer assets are absent. |
| 5 Licensing | Complete | Project MIT; plugin free; voicebank terms kept distinct; permissive nlohmann/json only; notices retained. |
| 6 Deliverables | Complete | VOCAL, SINGER PLATE, and reusable non-Rack core. |
| 7 Non-goals | Complete | English, CVVC, and VCCV are the approved post-spec additions; excluded renderer backends, effects, and unrelated formats remain excluded. |
| 8 Domain model | Complete | 480 PPQN score, notes/curves/vibrato/sections, optional phoneme position/preutterance/overlap/attack/release overrides, 64-bit arbitrary ticks, revisions, deterministic interpolation and overlap validation. |
| 9 Architecture | Complete | Layered core/voicebank/phonemizer/render/transport/import/DSP/Rack separation; documented in `ARCHITECTURE.md`. |
| 10 Real-time safety | Complete | `process()` uses atomics/immutable buffers/fixed DSP only; two-worker pool; cancel/generation handling; bar chunks with overlap; hazard-pointer reclamation; statuses and underrun counter. |
| 11 Voicebanks | Complete | Traditional `character.txt` and OpenUtau `character.yaml` folders, recursive oto, all six timing values, prefix map, metadata/images, UTF-8/BOM/UTF-16LE/CP932, PCM8/16/24/32, float32/64 and extensible RIFF/WAVE, `.frq`, external validation/relink/safe failure. CV, VCV, CVVC, VCCV, and X-SAMPA banks use the same physical loader. |
| 12 Phonemizers | Complete | Six selectable modes: English-to-Japanese for Japanese banks such as Adachi Rei, EN X-SAMPA, English VCCV, Japanese Auto CV/VCV, Japanese CVVC with CV fallback, and Direct Alias. Ordinary English uses packaged CMUdict 0.7b (133,000 entries) plus deterministic product-name/OOV fallback and exact bracketed phoneme hints. Multi-phone notes, OpenUtau-style connected-word coda transfer, contextual English-bank aliases, and `+*` hold / `+` syllable-advance chains render as timed connected events. |
| 13 Renderer | Complete | Native V1 `.frq`/YIN source pitch, consonant preservation, formant-preserving pitch-synchronous vowel synthesis, inherited plus editable oto timing/fades, curves, vibrato, transpose, arbitrary ranges, deterministic float audio, 44.1/48/96 kHz, mono/DC/limiter. |
| 14 Panel | Complete | 64 HP three-column Rack panel with a dominant piano-roll viewport, C-octave keyboard, readable lyric blocks, labeled dB dynamics lane, all 11 parameters, nine inputs, two outputs, native/custom tooltips, editor entry points and status lights. Linux x64, Windows x64, macOS x64 and macOS arm64 native build/ABI-harness jobs are defined. |
| 15 Transport | Complete | Continuous sample playhead, selectable PPQN, clock estimator with bounded phase correction and outlier rejection, warm reset, RUN/RESET/TRIG, armed one-shot, loop/END, sections, and four quantization choices. |
| 16 Modulation | Complete | Smoothed nondestructive PITCH ±12 semitones, DYN ±12 dB, added VIB, bounded FORM, plus render-domain transpose/tempo edits. |
| 17 Editor | Complete | Structured Rack overlay with File/Edit/View menus, compact Select/Draw/Erase/Slice tools, centered piano roll, marquee/Shift/Cmd+A selection, rigid multi-note group dragging, an editable note inspector, and separate lanes for sections, phoneme timing, pitch, and dynamics. The editor supports note editing, editable pipe-separated phoneme aliases, romaji-to-kana entry, keyboard traversal, mouse-held pitch audition, START/XFADE timing, snapping, navigation, transport, history, curves, sections, and import. The piano roll displays the rendered pitch with centered Standard portamento for adjacent notes and breaks over rests. Each action has contextual hover help. |
| 18 USTX import | Complete | Track scan/selection, note/pitch/dynamics/vibrato/first-phoneme timing/global mapping, foreign-setting and warning/ignored reports, malformed safety, deterministic fixtures. User-approved post-spec adapters also cover legacy UST and monophonic Standard MIDI without expanding renderer or phonemizer compatibility claims. |
| 19 Persistence | Complete | Versioned complete embedded score/settings, stable built-in singer, editor view/follow/snap state, schema migration, no serialized caches, missing-singer safe silence; real save/close/reopen passed. |
| 20 SINGER PLATE | Complete | Default and external singer image/name, fit/fill/name options, persistence, placeholder, no DSP/ports, Rack shared image cache. |
| 21 Multiple instances | Complete | Independent state and modulation with shared source decode; 3-instance harness/stress and 2-instance real-Rack +0/+7 evidence. |
| 22.1 Unit tests | Pass | `make -f tests/Makefile tests`: all required parser, encoding, alias, phonemizer, serialization, clock, transport, import and cache groups. Canonical EN X-SAMPA, English VCCV, English-to-Japanese and Japanese CVVC chains are asserted. The official-bank pass validates all 645 OTO entries, both transpose limits, and an audible multi-phone Adachi Rei English render. |
| 22.2 Offline integration | Pass | Official-bank tests cover finite output, duration, gaps, transpose, sample rates, timing overrides, and deterministic rendering. A 105-word Adachi matrix checks each phone's contribution, and a replacement test prevents canceled chunks from entering the cache. The Japanese Classic gate renders 628 Vocal notes and 625 Classic notes, then measures 543 boundaries, the available VCV aliases, 361 articulation-class joins, pitch, level, timbre, and expression. Its median zero-lag/best-lag boundary correlation is 0.915/0.931; median/p90 lag is 3/11 ms, median/p90 reliable pitch difference is 0.73/4.18 cents, and median log-mel correlation is 0.963. The English gate compares the same USTX and singer inputs with pinned OpenUtau for Adachi English-to-Japanese, EN X-SAMPA, and English VCCV. The Adachi leg aligns all 264 reference phones (100% recall and precision, 0 ms p90 timing); median best-lag correlation is 0.934, p90 lag is 12 ms, p90 pitch difference is 1.01 cents, and median log-mel correlation is 0.954. X-SAMPA and VCCV align all 16 phones with 0 ms p90 timing. All expression and audio-contribution gates pass. See `test-artifacts/openutau-regression/TEST_REPORT.md` and `test-artifacts/openutau-english-regression/TEST_REPORT.md`. The listener reported a close match in the Japanese A/C pair. Vocal keeps its smoother output instead of copying Classic's residual buzz. |
| 22.3 Rack harness | Pass | Actual `VocalModule`, lifecycle, sample-by-sample ports, all 16 required groups/behaviors. |
| 22.4 Real Rack E2E | Pass | Rack 2.6.6 loaded the packaged production and helper plugins. Scenarios A-K and a fresh-process save/reload passed on 2026-08-27. Coverage includes a triggered word, sustained-vowel CV, external ADSR/VCA, stock reverb, a complete song, six independent VOCAL modules, a 1.99333x clock ratio, and zero underruns in `results.json`. The 120 BPM capture stays continuous until END. This gate found a startup rerender-cancellation race, which now has a regression test. `editor-phoneme-timing.png` and `editor-inline-tab-next.png` capture editor timing and Tab navigation. |
| 23 Fixtures | Complete | Six-note reference, drone, one-shot, phrase bank, and eight-bar long fixture. |
| 24 Diagnostics | Complete | Required states, actionable alias/import/render errors, safe correction/rerender; malformed bank/import tests. |
| 25 Stability | Pass | Three modules for simulated five minutes, 1,000 resets, 100 create/delete cycles, repeated rerender/reclamation, zero post-preroll underruns; debug counters exposed. |
| 26 Build/CI/package | Complete | Rack make/dist flow, Docker Ubuntu core/offline flow, native tests, CI workflow, package asset/license validator and test-only E2E exclusion. |
| 27 Documentation | Complete | README, manual, architecture, notices, status, E2E report, voicebank convention/compatibility guidance, and reproducible same-USTX VocalRack/OpenUtau Japanese plus three-suite English A/C comparisons. |
| 28 Implementation order | Complete | All phases through release hardening performed; phase results are tests, not stopping points. |
| 29 Definition of Done | Complete | Corrected human listening acceptance is recorded; every machine-verifiable box passes. The production package and real-Rack artifacts were regenerated from the accepted renderer on 2026-08-27. |
| 30 References | Complete | Official Rack/OpenUtau/Mechanical Girl sources used; pinned decisions documented. |

## Test commands

```sh
make test
make docker-test
make openutau-compare
make openutau-english-regression
make release
VCVPLUGIN_PATH=dist/CosmicMatter-Vocal-2.0.0-mac-arm64.vcvplugin sh scripts/validate_release.sh
sh scripts/run_rack_e2e.sh
```

Machine-readable real-Rack results are in `test-artifacts/e2e/results.json`; `passed` is `true` and every named check is `true`.
