# VocalRack / OpenUtau project round-trip evidence

## Result

**PASS.** The production `vocalrack-render` CLI exported
`tests/fixtures/rei_phrase.json` to `exported-rei-phrase.ustx`. The pinned real
OpenUtau source/runtime loaded that generated file in an ephemeral Docker
container and rendered it through OpenUtau Classic with the bundled Adachi Rei
bank. VocalRack then re-imported the same generated USTX and rendered it again.

- OpenUtau singer: `足立レイ (adachi-rei)`
- OpenUtau renderer: `CLASSIC`
- OpenUtau phonemes: `あ だ ち れ e い i う`
- OpenUtau WAV: stereo, 16-bit, 44.1 kHz, 155714 frames
- VocalRack re-import WAV: mono, 16-bit, 44.1 kHz, 154350 frames
- Exported USTX SHA-256: `34ea32a0d22aff881163abdbced7a45c17e62b8cf0afbf22264f81616dc517dc`
- OpenUtau WAV SHA-256: `14c4336835d6bd12566ab27071edbb7033a2c92c4e73abc5b408a320d1650d28`
- VocalRack re-import WAV SHA-256: `e780f05c9b4353b6a8b5fac2ff3bcb66c5cd73bd6fa8e3fea2464000aa724415`

The automated C++ suite separately checks exact `.vocalrack` serialization,
USTX re-import tolerances for notes, lyrics, aliases, pitch, dynamics, vibrato,
and phoneme timing, plus transfer between independent Rack module instances.

The native `.vocalrack` file is the lossless interchange format. USTX carries
the score fields OpenUtau owns; Rack sections, CV/transport state, singer paths,
editor view state, and VocalRack-only attack/release timing deltas remain in the
native project rather than being written as misleading foreign fields.

Reproduce with `make openutau-roundtrip`.
