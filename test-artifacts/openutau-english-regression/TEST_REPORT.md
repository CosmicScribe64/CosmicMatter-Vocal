# VocalRack / pinned OpenUtau English regression

Overall result: **PASS**

The first row renders Vocal's exact shipped `Wake up, little machine` score and its production USTX export in pinned OpenUtau Classic. Every broad-corpus row renders the same generated USTX in both engines. Adachi Rei uses English-to-Japanese phonemization; the X-SAMPA and VCCV rows use deterministic, copyright-safe synthetic UTAU banks.

| Suite | Notes | Phones matched/reference | Recall / precision | Phone timing p90 | Boundaries | Best-lag r (median) | Lag p90 | Pitch p90 | Mel r (median) | Result |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Exact shipped Wake up, little machine template | 4 | 10/10 | 100.0% / 100.0% | 0.0 ms | 3 | 0.880 | 3.4 ms | 0.7 cents | 0.957 | PASS |
| Adachi Rei English to Japanese | 119 | 264/264 | 100.0% / 100.0% | 0.0 ms | 100 | 0.938 | 11.0 ms | 1.0 cents | 0.954 | PASS |
| English X-SAMPA | 11 | 16/16 | 100.0% / 100.0% | 0.0 ms | 5 | 0.953 | 4.6 ms | 0.3 cents | 0.979 | PASS |
| English VCCV | 9 | 16/16 | 100.0% / 100.0% | 0.0 ms | 5 | 0.863 | 7.6 ms | 0.3 cents | 0.978 | PASS |

## Evidence

Each suite checks that both render logs contain the canonical phoneme aliases in the same order. It also checks aligned phone timing, boundary and transient behavior, sustained pYIN/YIN pitch, RMS level, log-mel envelope, spectral centroid and flatness, and low- and high-band ratios. The report includes per-group plots, the twelve worst boundaries, and a sequential A/C listening WAV for the eight worst cases. The exact-template row includes the shipped pitches, pitch/dynamics curves, and vibrato. The broad Adachi corpus contains 119 notes in 25 phrases. It covers ordinary dictionary words, major consonant classes, clusters, diphthongs, multisyllabic lyrics, connected phrases, continuations, registers, and explicit internal consonant boundaries.

Reproduce with:

```sh
make openutau-english-regression
```

Failed checks: none.
