#!/usr/bin/env python3
import hashlib
import pathlib
import re
import sys
import wave


out = pathlib.Path(sys.argv[1])
ustx = out / "exported-rei-phrase.ustx"
wav = out / "openutau-classic.wav"
native_wav = out / "vocalrack-reimport.wav"
log = (out / "openutau-classic.log").read_text(encoding="utf-8", errors="replace")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def wav_info(path: pathlib.Path):
    with wave.open(str(path), "rb") as stream:
        return stream.getnchannels(), stream.getsampwidth() * 8, stream.getframerate(), stream.getnframes()


required = [
    "OpenUtau singer: 足立レイ (adachi-rei)",
    "OpenUtau renderer: CLASSIC",
    "OpenUtau phonemes:",
]
missing = [item for item in required if item not in log]
if missing:
    raise SystemExit("round-trip validation failed: " + ", ".join(missing))
phonemes = re.search(r"OpenUtau phonemes: (.+)", log)
channels, bits, rate, frames = wav_info(wav)
n_channels, n_bits, n_rate, n_frames = wav_info(native_wav)
if channels != 2 or bits != 16 or rate != 44100 or frames <= 0:
    raise SystemExit("round-trip validation failed: unexpected OpenUtau WAV format")
if n_channels != 1 or n_bits != 16 or n_rate != 44100 or n_frames <= 0:
    raise SystemExit("round-trip validation failed: unexpected VocalRack re-import WAV format")

report = f"""# VocalRack / OpenUtau project round-trip evidence

## Result

**PASS.** The production `vocalrack-render` CLI exported
`tests/fixtures/rei_phrase.json` to `exported-rei-phrase.ustx`. The pinned real
OpenUtau source/runtime loaded that generated file in an ephemeral Docker
container and rendered it through OpenUtau Classic with the bundled Adachi Rei
bank. VocalRack then re-imported the same generated USTX and rendered it again.

- OpenUtau singer: `足立レイ (adachi-rei)`
- OpenUtau renderer: `CLASSIC`
- OpenUtau phonemes: `{phonemes.group(1).strip() if phonemes else 'not reported'}`
- OpenUtau WAV: stereo, 16-bit, 44.1 kHz, {frames} frames
- VocalRack re-import WAV: mono, 16-bit, 44.1 kHz, {n_frames} frames
- Exported USTX SHA-256: `{sha256(ustx)}`
- OpenUtau WAV SHA-256: `{sha256(wav)}`
- VocalRack re-import WAV SHA-256: `{sha256(native_wav)}`

The automated C++ suite separately checks exact `.vocalrack` serialization,
USTX re-import tolerances for notes, lyrics, aliases, pitch, dynamics, vibrato,
and phoneme timing, plus transfer between independent Rack module instances.

The native `.vocalrack` file is the lossless interchange format. USTX carries
the score fields OpenUtau owns; Rack sections, CV/transport state, singer paths,
editor view state, and VocalRack-only attack/release timing deltas remain in the
native project rather than being written as misleading foreign fields.

Reproduce with `make openutau-roundtrip`.
"""
(out / "TEST_REPORT.md").write_text(report, encoding="utf-8")
print(report.splitlines()[3])
