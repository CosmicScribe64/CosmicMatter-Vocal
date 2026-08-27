#!/usr/bin/env python3
"""Evaluate a global VocalRack timing advance without rewriting WAV evidence."""

import argparse
import json
import pathlib
import wave

import numpy as np


def load_pcm16(path: pathlib.Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as wav:
        rate = wav.getframerate()
        channels = wav.getnchannels()
        values = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2")
    return values.reshape(-1, channels).mean(axis=1).astype(np.float64) / 32768.0, rate


def envelope(signal: np.ndarray, rate: int) -> tuple[np.ndarray, np.ndarray]:
    window = max(1, round(rate * 0.005))
    hop = max(1, round(rate * 0.001))
    power = np.convolve(signal * signal, np.ones(window) / window, mode="same")
    samples = np.arange(0, len(signal), hop)
    return samples / rate, np.sqrt(power[samples])


def correlation(left: np.ndarray, right: np.ndarray) -> float:
    left = left - np.mean(left)
    right = right - np.mean(right)
    denominator = float(np.linalg.norm(left) * np.linalg.norm(right))
    return float(np.dot(left, right) / denominator) if denominator > 1e-12 else 0.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("vocalrack", type=pathlib.Path)
    parser.add_argument("classic", type=pathlib.Path)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("--minimum-ms", type=int, default=-12)
    parser.add_argument("--maximum-ms", type=int, default=12)
    args = parser.parse_args()

    vocal, vocal_rate = load_pcm16(args.vocalrack)
    classic, classic_rate = load_pcm16(args.classic)
    if vocal_rate != classic_rate:
        raise ValueError("sample rates differ")
    vocal /= np.max(np.abs(vocal))
    classic /= np.max(np.abs(classic))
    vocal_times, vocal_envelope = envelope(vocal, vocal_rate)
    classic_times, classic_envelope = envelope(classic, classic_rate)
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))

    print("advance_ms\tall_median\tcontinuation_median\tcv_median\tduration_median\t"
          "join_median\tpitch_median\tword_split_median")
    for advance_ms in range(args.minimum_ms, args.maximum_ms + 1):
        groups: dict[str, list[float]] = {}
        all_values: list[float] = []
        for boundary in manifest["boundaries"]:
            center = float(boundary["seconds"])
            grid = np.arange(center - 0.120, center + 0.160, 0.001)
            # Advancing VocalRack by N ms means its original future sample at
            # t+N is heard at t. The Classic reference remains score-aligned.
            left = np.interp(grid + advance_ms / 1000.0,
                             vocal_times, vocal_envelope, left=0, right=0)
            right = np.interp(grid, classic_times, classic_envelope, left=0, right=0)
            value = correlation(left, right)
            all_values.append(value)
            group = str(boundary["group"]).split(":", 1)[0]
            groups.setdefault(group, []).append(value)

        median = lambda name: float(np.median(groups.get(name, [0.0])))
        print(f"{advance_ms:+d}\t{np.median(all_values):.6f}\t{median('continuation'):.6f}"
              f"\t{median('cv'):.6f}\t{median('duration'):.6f}"
              f"\t{median('join-matrix'):.6f}\t{median('pitch'):.6f}"
              f"\t{median('word-split'):.6f}")


if __name__ == "__main__":
    main()
