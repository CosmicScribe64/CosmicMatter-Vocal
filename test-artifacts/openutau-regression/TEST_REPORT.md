# VocalRack / OpenUtau Classic Adachi Rei regression

Overall result: **PASS**

Both renders use the same generated/fixed USTX fixtures and the bundled official Adachi Rei bank. The reference is pinned OpenUtau Classic with its built-in `worldline` resampler and `convergence` wavtool. The goal is perceptual and performance equivalence, not phase-identical samples.

## Coverage

- 628 VocalRack notes and 625 OpenUtau notes rendered.
- 543 boundaries measured, including all available VCV aliases and all ordered articulation-class joins.
- 612 of 613 eligible sustained notes measured for pitch (99.84% coverage); 15 sub-180 ms consonant-dominant notes remain covered by boundary/duration checks.
- 628 notes measured for timbre (100.00% coverage).
- Separate flat pitch, two authored vibratos (including phase shift), an authored pitch curve, and a sparse dynamics curve.
- All bundled OTO entries are validated by the native official-bank test; this corpus exercises the normal Japanese CV/VCV linguistic inventory.

## Corpus results

| Measure | Result |
|---|---:|
| Median zero-lag boundary correlation | 0.9171 |
| Median best-lag boundary correlation | 0.9315 |
| P10 best-lag boundary correlation | 0.7014 |
| Median / p90 absolute boundary lag | 3.0 / 9.0 ms |
| Median / p90 A:C pitch difference | 0.73 / 4.15 cents |
| Worst reliable A:C pitch difference | 11.63 cents |
| Worst VocalRack target-pitch error | 15.00 cents |
| Median / p10 log-mel correlation | 0.9636 / 0.9018 |
| Median / p90 log-mel RMSE | 4.96 / 8.19 dB |
| P90 absolute level difference | 1.93 dB |
| P90 warmth / high-band ratio difference | 0.0802 / 0.0775 |
| Continuations: median best-lag correlation / lag | 0.7873 / 1.0 ms |
| Continuations: median pitch difference / log-mel correlation | 0.06 cents / 0.9703 |

## Expression results

| Case | Best correlation | Aligned RMSE | Lag |
|---|---:|---:|---:|
| flat-control | target error A 0.11 cents | A:C median 0.08 cents | n/a |
| vibrato-5hz | 0.9472 | 8.39 cents | +0 ms |
| vibrato-shifted-6.25hz | 0.9783 | 1.93 cents | -5 ms |
| authored-pitch-curve | 0.9982 | 1.16 cents | -5 ms |
| authored-dynamics-curve | 0.9991 | 0.173 dB | +5 ms |

## Reproduce

```sh
make openutau-regression
```

OpenUtau, .NET/NuGet, librosa, NumPy, SciPy, and plotting run in disposable Docker containers. Inspect `regression-report.json`, `expression-report.json`, the separate shared-scale PNG plots, and `regression-worst-ac-sequential.wav` for machine-readable and listening evidence.

Failed checks: none.
