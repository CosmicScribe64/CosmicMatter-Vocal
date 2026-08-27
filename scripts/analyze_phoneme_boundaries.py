#!/usr/bin/env python3
"""Compare syllable-boundary envelopes without conflating them with carrier phase."""

import json
import pathlib
import sys

import librosa
import matplotlib.pyplot as plt
import numpy as np


BOUNDARIES = [
    (0.500, "a to da"),
    (1.000, "da to chi"),
    (1.500, "chi to re"),
    (2.000, "re to i"),
    (3.000, "i to u"),
]


def load(path: pathlib.Path) -> tuple[np.ndarray, int]:
    signal, rate = librosa.load(path, sr=None, mono=True)
    signal = signal.astype(np.float64)
    peak = float(np.max(np.abs(signal)))
    return signal / peak if peak > 1e-12 else signal, int(rate)


def rms_envelope(signal: np.ndarray, rate: int) -> tuple[np.ndarray, np.ndarray]:
    window = max(1, round(rate * 0.005))
    hop = max(1, round(rate * 0.001))
    power = np.convolve(signal * signal, np.ones(window) / window, mode="same")
    samples = np.arange(0, len(signal), hop)
    return samples / rate, np.sqrt(power[samples])


def correlation(left: np.ndarray, right: np.ndarray) -> float:
    if len(left) < 3 or len(left) != len(right):
        return 0.0
    left = left - np.mean(left)
    right = right - np.mean(right)
    denominator = float(np.linalg.norm(left) * np.linalg.norm(right))
    return float(np.dot(left, right) / denominator) if denominator > 1e-12 else 0.0


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: analyze_phoneme_boundaries.py VOCALRACK.wav OPENUTAU_CLASSIC.wav OUT_DIR")
    vocal_path, classic_path, out = map(pathlib.Path, sys.argv[1:])
    out.mkdir(parents=True, exist_ok=True)
    vocal, vocal_rate = load(vocal_path)
    classic, classic_rate = load(classic_path)
    if vocal_rate != classic_rate:
        raise ValueError(f"sample-rate mismatch: {vocal_rate} versus {classic_rate}")
    vocal_times, vocal_env = rms_envelope(vocal, vocal_rate)
    classic_times, classic_env = rms_envelope(classic, classic_rate)

    figure, axes = plt.subplots(len(BOUNDARIES), 1, figsize=(13, 13), sharex=False, constrained_layout=True)
    results = []
    before, after = 0.160, 0.200
    for axis, (boundary, label) in zip(axes, BOUNDARIES):
        grid = np.arange(boundary - before, boundary + after, 0.001)
        vocal_local = np.interp(grid, vocal_times, vocal_env, left=0.0, right=0.0)
        classic_local = np.interp(grid, classic_times, classic_env, left=0.0, right=0.0)
        best_lag, best_correlation = 0, -2.0
        for lag in range(-50, 51):
            if lag < 0:
                left, right = vocal_local[-lag:], classic_local[:lag]
            elif lag > 0:
                left, right = vocal_local[:-lag], classic_local[lag:]
            else:
                left, right = vocal_local, classic_local
            value = correlation(left, right)
            if value > best_correlation:
                best_lag, best_correlation = lag, value
        relative_ms = (grid - boundary) * 1000.0
        axis.plot(relative_ms, vocal_local, color="#d33f6a", lw=1.5, label="VocalRack")
        axis.plot(relative_ms, classic_local, color="#2b78d0", lw=1.5, label="OpenUtau Classic")
        axis.axvline(0, color="#202020", lw=1.0, ls="--", alpha=0.7)
        axis.set_title(
            f"{label} at {boundary:.1f} s: envelope correlation {correlation(vocal_local, classic_local):.3f}, "
            f"best lag {best_lag:+d} ms",
            loc="left", fontsize=11, fontweight="bold")
        axis.set_ylabel("5 ms RMS\n(peak-normalized)")
        axis.set_xlim(-before * 1000.0, after * 1000.0)
        axis.grid(alpha=0.18)
        results.append({
            "boundarySeconds": boundary,
            "label": label,
            "zeroLagCorrelation": correlation(vocal_local, classic_local),
            "bestLagMsOpenUtauRelativeToVocalRack": best_lag,
            "bestLagCorrelation": best_correlation,
            "vocalRackArea": float(np.trapezoid(vocal_local, grid)),
            "openUtauClassicArea": float(np.trapezoid(classic_local, grid)),
        })
    axes[0].legend(loc="upper right")
    axes[-1].set_xlabel("Time relative to authored note boundary (ms)")
    figure.suptitle("A/C phoneme formation: boundary envelope detail", fontsize=16, fontweight="bold")
    figure.savefig(out / "phoneme-boundary-zoom.png", dpi=190)
    plt.close(figure)
    (out / "phoneme-boundaries.json").write_text(
        json.dumps({"windowMs": [-160, 200], "rmsWindowMs": 5, "boundaries": results}, indent=2) + "\n")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
