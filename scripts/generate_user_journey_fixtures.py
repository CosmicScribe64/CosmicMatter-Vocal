#!/usr/bin/env python3
"""Generate deterministic Standard MIDI fixtures for the installed-Rack UX audit."""

from __future__ import annotations

import pathlib
import struct
import sys


def chunk(kind: bytes, payload: bytes) -> bytes:
    return kind + struct.pack(">I", len(payload)) + payload


def header(track_count: int) -> bytes:
    return chunk(b"MThd", struct.pack(">HHH", 1, track_count, 480))


def conductor() -> bytes:
    return chunk(
        b"MTrk",
        bytes((0, 0xFF, 0x03, 9))
        + b"Conductor"
        + bytes((0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20,
                 0, 0xFF, 0x58, 4, 4, 2, 24, 8,
                 0, 0xFF, 0x2F, 0)),
    )


def melody(name: bytes = b"Melody", lyrics: bool = True, polyphonic: bool = False) -> bytes:
    events = bytearray((0, 0xFF, 0x03, len(name)))
    events.extend(name)
    if lyrics:
        events.extend((0, 0xFF, 0x05, 3, 0xE3, 0x81, 0x82))  # あ
    events.extend((0, 0x90, 60, 100))
    if polyphonic:
        events.extend((0, 0x90, 64, 100))
    events.extend((0x83, 0x60, 0x80, 60, 0))
    if polyphonic:
        events.extend((0, 0x80, 64, 0))
    if lyrics:
        events.extend((0, 0xFF, 0x05, 3, 0xE3, 0x81, 0x84))  # い
    events.extend((0, 0x90, 64, 100,
                   0x81, 0x70, 0x80, 64, 0,
                   0x78, 0x90, 67, 100,
                   0x83, 0x60, 0x80, 67, 0,
                   0, 0xFF, 0x2F, 0))
    return chunk(b"MTrk", bytes(events))


def write(path: pathlib.Path, tracks: list[bytes]) -> None:
    path.write_bytes(header(len(tracks)) + b"".join(tracks))


def main() -> int:
    output = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "test-artifacts/user-journey/fixtures")
    output.mkdir(parents=True, exist_ok=True)
    base = conductor()
    write(output / "melody-with-lyrics.mid", [base, melody()])
    write(output / "melody-no-lyrics.mid", [base, melody(lyrics=False)])
    write(output / "mixed-poly-mono.mid", [base, melody(b"Chords", polyphonic=True), melody(b"Vocal")])
    write(output / "all-polyphonic.mid", [base, melody(b"Chords", polyphonic=True)])
    print(f"Wrote MIDI user-journey fixtures to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
