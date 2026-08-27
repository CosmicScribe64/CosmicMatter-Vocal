#!/usr/bin/env python3
"""Measure and create one VocalRack/OpenUtau listening comparison."""

import json
import math
import pathlib
import statistics
import struct
import sys
import wave


def read_mono(path: pathlib.Path) -> tuple[int, list[float], int]:
    with wave.open(str(path), "rb") as wav:
        rate, channels, width = wav.getframerate(), wav.getnchannels(), wav.getsampwidth()
        raw = wav.readframes(wav.getnframes())
    if width != 2 or channels not in (1, 2):
        raise ValueError(f"{path}: expected mono/stereo PCM16")
    ints = struct.unpack("<" + "h" * (len(raw) // 2), raw)
    samples = [sum(ints[i:i + channels]) / (32768.0 * channels) for i in range(0, len(ints), channels)]
    return rate, samples, channels


def metrics(samples: list[float], rate: int, channels: int) -> dict:
    peak = max((abs(value) for value in samples), default=0.0)
    rms = math.sqrt(sum(value * value for value in samples) / max(1, len(samples)))
    threshold = max(1e-4, peak * 0.002)
    active = [i for i, value in enumerate(samples) if abs(value) >= threshold]
    begin = active[0] if active else 0
    end = active[-1] + 1 if active else 0
    return {
        "sampleRate": rate,
        "sourceChannels": channels,
        "frames": len(samples),
        "seconds": len(samples) / rate,
        "peak": peak,
        "rms": rms,
        "firstSoundSeconds": begin / rate,
        "lastSoundSeconds": end / rate,
        "activeSeconds": (end - begin) / rate,
    }


def envelope(samples: list[float], rate: int, window_ms: int = 10) -> list[float]:
    size = max(1, round(rate * window_ms / 1000))
    return [math.sqrt(sum(x * x for x in samples[i:i + size]) / max(1, len(samples[i:i + size])))
            for i in range(0, len(samples), size)]


def correlation(a: list[float], b: list[float]) -> float:
    if len(a) < 2 or len(b) != len(a):
        return 0.0
    ma, mb = statistics.fmean(a), statistics.fmean(b)
    da, db = [x - ma for x in a], [x - mb for x in b]
    denom = math.sqrt(sum(x * x for x in da) * sum(x * x for x in db))
    return sum(x * y for x, y in zip(da, db)) / denom if denom else 0.0


def best_envelope_lag(a: list[float], b: list[float], max_lag: int = 100) -> tuple[int, float]:
    best = (0, -2.0)
    for lag in range(-max_lag, max_lag + 1):
        start_a, start_b = max(0, -lag), max(0, lag)
        count = min(len(a) - start_a, len(b) - start_b)
        if count < 20:
            continue
        value = correlation(a[start_a:start_a + count], b[start_b:start_b + count])
        if value > best[1]:
            best = (lag, value)
    return best


def normalize(samples: list[float], target: float = 0.85) -> list[float]:
    peak = max((abs(value) for value in samples), default=0.0)
    gain = target / peak if peak > 1e-9 else 1.0
    return [max(-1.0, min(1.0, value * gain)) for value in samples]


def write_pcm16(path: pathlib.Path, rate: int, channels: int, values: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    packed = struct.pack("<" + "h" * len(values), *(round(max(-1, min(1, x)) * 32767) for x in values))
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(packed)


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: analyze_ab.py VOCALRACK.wav OPENUTAU.wav OUTPUT_DIR PREFIX OPENUTAU_LABEL")
    vocal_path, openutau_path, output_dir = map(pathlib.Path, sys.argv[1:4])
    prefix, openutau_label = sys.argv[4:]
    if prefix not in ("ab", "ac"):
        raise ValueError("comparison prefix must be 'ab' or 'ac'")
    output_dir.mkdir(parents=True, exist_ok=True)
    rate_a, vocal, channels_a = read_mono(vocal_path)
    rate_b, openutau, channels_b = read_mono(openutau_path)
    if rate_a != rate_b:
        raise ValueError(f"sample-rate mismatch: {rate_a} versus {rate_b}")

    env_a, env_b = envelope(vocal, rate_a), envelope(openutau, rate_a)
    lag_windows, envelope_corr = best_envelope_lag(env_a, env_b)
    lag_samples = round(lag_windows * rate_a * 0.010)
    vocal_n, openutau_n = normalize(vocal), normalize(openutau)

    # Stereo is onset-aligned and level-normalized: VocalRack left, OpenUtau right.
    offset_a, offset_b = (max(0, lag_samples), max(0, -lag_samples))
    stereo_frames = max(offset_a + len(vocal_n), offset_b + len(openutau_n))
    stereo: list[float] = []
    for i in range(stereo_frames):
        left = vocal_n[i - offset_a] if offset_a <= i < offset_a + len(vocal_n) else 0.0
        right = openutau_n[i - offset_b] if offset_b <= i < offset_b + len(openutau_n) else 0.0
        stereo.extend((left, right))
    write_pcm16(output_dir / f"{prefix}-aligned-stereo.wav", rate_a, 2, stereo)

    # Sequential is easier to judge without headphones: VocalRack, 0.5 s, OpenUtau.
    silence = [0.0] * round(rate_a * 0.5)
    write_pcm16(output_dir / f"{prefix}-sequential.wav", rate_a, 1, vocal_n + silence + openutau_n)

    report = {
        "fixture": str(pathlib.Path("tests/fixtures/adachi_rei_ab.ustx")),
        "order": {"sequential": ["VocalRack", openutau_label], "stereo": {"left": "VocalRack", "right": openutau_label}},
        "vocalRack": metrics(vocal, rate_a, channels_a),
        "openUtauLabel": openutau_label,
        "openUtau": metrics(openutau, rate_b, channels_b),
        "alignment": {
            "openUtauLagSecondsRelativeToVocalRack": lag_samples / rate_a,
            "tenMsEnvelopeCorrelation": envelope_corr,
            "searchRangeSeconds": 1.0,
        },
        "notes": [
            "Source WAV metrics preserve renderer output level; listening composites normalize each renderer independently.",
            "Correlation compares 10 ms RMS envelopes instead of sample waveforms because the synthesis algorithms differ.",
        ],
    }
    (output_dir / f"{prefix}-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
