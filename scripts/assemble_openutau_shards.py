#!/usr/bin/env python3
"""Place bounded OpenUtau regression renders on their original time axis."""

import argparse
import json
import pathlib
import wave

import numpy as np


def load_mono(path: pathlib.Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as wav:
        rate = wav.getframerate()
        channels = wav.getnchannels()
        if wav.getsampwidth() != 2:
            raise ValueError(f"{path}: expected PCM16")
        values = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2")
    return values.reshape(-1, channels).mean(axis=1).astype(np.float64) / 32768.0, rate


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    expected_rate = int(manifest["sampleRate"])
    loaded = []
    total_frames = round(float(manifest["durationSeconds"]) * expected_rate)
    for shard in manifest["shards"]:
        signal, rate = load_mono(args.manifest.parent / shard["output"])
        if rate != expected_rate:
            raise ValueError(f"{shard['output']}: sample-rate mismatch {rate} != {expected_rate}")
        start = round(float(shard["startSeconds"]) * rate)
        total_frames = max(total_frames, start + len(signal))
        loaded.append((start, signal))
    combined = np.zeros(total_frames, dtype=np.float64)
    for start, signal in loaded:
        combined[start:start + len(signal)] += signal
    peak = float(np.max(np.abs(combined)))
    if peak > 1.0:
        combined /= peak
    with wave.open(str(args.output), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(expected_rate)
        wav.writeframes(np.rint(np.clip(combined, -1, 1) * 32767).astype("<i2").tobytes())
    print(json.dumps({"shards": len(loaded), "frames": len(combined),
                      "durationSeconds": len(combined) / expected_rate, "peakBeforeLimit": peak}))


if __name__ == "__main__":
    main()
