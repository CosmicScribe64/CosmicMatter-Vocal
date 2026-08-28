# Changelog

All notable user-visible changes to Cosmic Matter: Vocal are recorded here.
Versions follow VCV Rack's convention: the major version matches the supported
Rack major version.

## [2.0.1] - 2026-08-27

VCV Library submission-readiness patch.

- Removed the external Windows `libiconv` build/runtime dependency by using
  native Windows text conversion for CP932 and UTF-16LE voicebank metadata.
- Added CP932 and UTF-16LE regression coverage, including malformed-input
  rejection.
- Made all four platform CI jobs verify VCV's clean plugin build contract:
  `make clean`, `make cleandep`, `make dep`, and `make dist`.

## [2.0.0] - 2026-08-27

Initial public release for VCV Rack 2.

- Native monophonic vocal workstation with a Rack-hosted piano-roll editor.
- UTAU voicebank loading with bundled Adachi Rei 3.5.0 first-sound content.
- Japanese Auto, Japanese CVVC, English-to-Japanese, EN X-SAMPA, English VCCV,
  and Direct Alias phonemizers.
- OpenUtau USTX, UTAU UST, Standard MIDI, and lossless `.vocalrack` import.
- USTX and lossless project export.
- Internal/external transport, phrase triggering, looping, sections, END pulse,
  expression CV, pitch curves, dynamics, vibrato, and phoneme timing.
- OpenUtau-style monophonic collision editing: newly drawn, moved, resized, or
  pasted notes take priority and trim/remove covered notes in one undoable edit.
- Independently draggable internal phoneme dividers with clamping, undo,
  lossless project persistence, rendering, and OpenUtau indexed-offset exchange.
- OpenUtau-style continuous phoneme regions: adjacent sounds meet at a shared
  boundary, while positive overlap is shown as an X-shaped crossfade.
- Cross-platform CI definitions for Linux x64, Windows x64, macOS x64, and
  macOS arm64.
- Automated renderer comparisons against pinned OpenUtau Classic plus a real
  VCV Rack end-to-end interaction and audio suite.

[2.0.1]: https://github.com/CosmicScribe64/CosmicMatter-Vocal/compare/v2.0.0...main
[2.0.0]: https://github.com/CosmicScribe64/CosmicMatter-Vocal/releases/tag/v2.0.0
