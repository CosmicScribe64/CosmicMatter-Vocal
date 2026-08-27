#!/usr/bin/env python3
"""Compare authored pitch, vibrato, and dynamics against OpenUtau Classic."""

import argparse
import json
import math
import pathlib
import wave

import librosa
import matplotlib.pyplot as plt
import numpy as np


def load_audio(path: pathlib.Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as wav:
        rate = wav.getframerate()
        channels = wav.getnchannels()
        if wav.getsampwidth() != 2:
            raise ValueError(f"{path}: expected PCM16")
        values = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2")
    return values.reshape(-1, channels).mean(axis=1).astype(np.float32) / 32768.0, rate


def correlation(left: np.ndarray, right: np.ndarray) -> float:
    left = left - np.mean(left)
    right = right - np.mean(right)
    denominator = float(np.linalg.norm(left) * np.linalg.norm(right))
    return float(np.dot(left, right) / denominator) if denominator > 1e-12 else 0.0


def aligned_metrics(left: np.ndarray, right: np.ndarray, step_ms: int = 5) -> dict:
    zero = correlation(left, right)
    best_lag, best_corr = 0, zero
    maximum_lag_frames = max(1, 30 // step_ms)
    for lag in range(-maximum_lag_frames, maximum_lag_frames + 1):
        if lag < 0:
            a, b = left[-lag:], right[:lag]
        elif lag > 0:
            a, b = left[:-lag], right[lag:]
        else:
            a, b = left, right
        value = correlation(a, b)
        if value > best_corr:
            best_lag, best_corr = lag, value
    if best_lag < 0:
        aligned_left, aligned_right = left[-best_lag:], right[:best_lag]
    elif best_lag > 0:
        aligned_left, aligned_right = left[:-best_lag], right[best_lag:]
    else:
        aligned_left, aligned_right = left, right
    return {
        "zeroLagCorrelation": zero,
        "bestLagCorrelation": best_corr,
        "bestLagMsOpenUtauRelativeToVocalRack": best_lag * step_ms,
        "alignedRmse": float(np.sqrt(np.mean((aligned_left - aligned_right) ** 2))),
    }


def target_aware_pitch(signal: np.ndarray, rate: int, target_hz: float) -> tuple[np.ndarray, np.ndarray]:
    hop = 128
    pyin, voiced, _ = librosa.pyin(
        signal, fmin=max(50.0, target_hz / 3.0), fmax=min(1200.0, target_hz * 4.0),
        sr=rate, frame_length=4096, hop_length=hop)
    yin = librosa.yin(
        signal, fmin=max(50.0, target_hz / 3.0), fmax=min(1200.0, target_hz * 4.0),
        sr=rate, frame_length=4096, hop_length=hop)
    selected = np.asarray(yin, dtype=np.float64)
    valid_pyin = voiced & np.isfinite(pyin)
    pyin_error = np.full(len(selected), np.inf)
    pyin_error[valid_pyin] = np.abs(np.log2(pyin[valid_pyin] / target_hz))
    yin_error = np.abs(np.log2(selected / target_hz))
    use_pyin = pyin_error <= yin_error
    selected[use_pyin] = pyin[use_pyin]
    # The broad corpus independently gates octave correctness. Here, keep rare
    # H2/H3 tracker locks from obscuring the small authored expression contour.
    cents = 1200.0 * np.log2(selected / target_hz)
    cents -= np.round(cents / 1200.0) * 1200.0
    times = librosa.frames_to_time(np.arange(len(cents)), sr=rate, hop_length=hop)
    return times, cents


def dominant_rate(values: np.ndarray, step_seconds: float) -> float:
    centered = values - np.mean(values)
    windowed = centered * np.hanning(len(centered))
    frequencies = np.fft.rfftfreq(8192, step_seconds)
    spectrum = np.abs(np.fft.rfft(windowed, 8192))
    mask = (frequencies >= 2.0) & (frequencies <= 10.0)
    return float(frequencies[mask][np.argmax(spectrum[mask])])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("vocalrack", type=pathlib.Path)
    parser.add_argument("classic", type=pathlib.Path)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("thresholds", type=pathlib.Path)
    parser.add_argument("output_dir", type=pathlib.Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    thresholds = json.loads(args.thresholds.read_text(encoding="utf-8"))
    vocal, vocal_rate = load_audio(args.vocalrack)
    classic, classic_rate = load_audio(args.classic)
    if vocal_rate != classic_rate:
        raise ValueError(f"sample-rate mismatch {vocal_rate} != {classic_rate}")
    target_hz = float(librosa.midi_to_hz(manifest["targetMidi"]))
    vocal_times, vocal_cents = target_aware_pitch(vocal, vocal_rate, target_hz)
    classic_times, classic_cents = target_aware_pitch(classic, classic_rate, target_hz)

    step_seconds = 0.005
    pitch_rows = []
    pitch_series = []
    for segment in manifest["pitchSegments"]:
        grid = np.arange(segment["startSeconds"], segment["endSeconds"], step_seconds)
        left = np.interp(grid, vocal_times, vocal_cents)
        right = np.interp(grid, classic_times, classic_cents)
        metrics = aligned_metrics(left, right)
        aligned_rmse = metrics.pop("alignedRmse")
        row = {**segment, **metrics,
               "alignedRmseCents": aligned_rmse,
               "vocalRackMedianCents": float(np.median(left)),
               "openUtauClassicMedianCents": float(np.median(right))}
        if segment["kind"] == "vibrato":
            vocal_depth = float((np.percentile(left, 95) - np.percentile(left, 5)) / 2.0)
            classic_depth = float((np.percentile(right, 95) - np.percentile(right, 5)) / 2.0)
            vocal_rate_hz = dominant_rate(left, step_seconds)
            classic_rate_hz = dominant_rate(right, step_seconds)
            row.update({
                "vocalRackDepthCents": vocal_depth,
                "openUtauClassicDepthCents": classic_depth,
                "depthDifferenceCents": vocal_depth - classic_depth,
                "vocalRackRateHz": vocal_rate_hz,
                "openUtauClassicRateHz": classic_rate_hz,
                "rateDifferenceHz": vocal_rate_hz - classic_rate_hz,
                "vocalRackExpectedRateErrorHz": vocal_rate_hz - segment["expectedRateHz"],
                "openUtauExpectedRateErrorHz": classic_rate_hz - segment["expectedRateHz"],
            })
        pitch_rows.append(row)
        pitch_series.append((segment, grid, left, right))

    envelope_hop = 128
    frame_length = 2048
    vocal_rms = librosa.feature.rms(y=vocal, frame_length=frame_length, hop_length=envelope_hop)[0]
    classic_rms = librosa.feature.rms(y=classic, frame_length=frame_length, hop_length=envelope_hop)[0]
    vocal_envelope_times = librosa.frames_to_time(
        np.arange(len(vocal_rms)), sr=vocal_rate, hop_length=envelope_hop)
    classic_envelope_times = librosa.frames_to_time(
        np.arange(len(classic_rms)), sr=classic_rate, hop_length=envelope_hop)
    dynamics_rows = []
    dynamics_series = []
    for segment in manifest["dynamicsSegments"]:
        grid = np.arange(segment["startSeconds"], segment["endSeconds"], step_seconds)
        left = 20.0 * np.log10(np.maximum(
            np.interp(grid, vocal_envelope_times, vocal_rms), 1e-8))
        right = 20.0 * np.log10(np.maximum(
            np.interp(grid, classic_envelope_times, classic_rms), 1e-8))
        left -= np.median(left)
        right -= np.median(right)
        metrics = aligned_metrics(left, right)
        aligned_rmse = metrics.pop("alignedRmse")
        row = {**segment, **metrics,
               "alignedRmseDb": aligned_rmse,
               "vocalRackSpanDb": float(np.percentile(left, 95) - np.percentile(left, 5)),
               "openUtauClassicSpanDb": float(np.percentile(right, 95) - np.percentile(right, 5))}
        dynamics_rows.append(row)
        dynamics_series.append((segment, grid, left, right))

    checks = {}
    flat = next(row for row in pitch_rows if row["kind"] == "flat")
    checks["flatVocalRackTarget"] = abs(flat["vocalRackMedianCents"]) <= thresholds["maximumFlatTargetErrorCents"]
    checks["flatClassicTarget"] = abs(flat["openUtauClassicMedianCents"]) <= thresholds["maximumFlatTargetErrorCents"]
    checks["flatDifference"] = abs(flat["vocalRackMedianCents"] - flat["openUtauClassicMedianCents"]) <= thresholds["maximumFlatDifferenceCents"]
    for row in pitch_rows:
        if row["kind"] == "flat":
            continue
        checks[f"pitchCorrelation:{row['name']}"] = row["bestLagCorrelation"] >= thresholds["minimumPitchContourCorrelation"]
        checks[f"pitchRmse:{row['name']}"] = row["alignedRmseCents"] <= thresholds["maximumPitchContourRmseCents"]
        checks[f"pitchLag:{row['name']}"] = abs(row["bestLagMsOpenUtauRelativeToVocalRack"]) <= thresholds["maximumPitchContourAbsoluteLagMs"]
        if row["kind"] == "vibrato":
            checks[f"vibratoDepth:{row['name']}"] = abs(row["depthDifferenceCents"]) <= thresholds["maximumVibratoDepthDifferenceCents"]
            checks[f"vibratoRate:{row['name']}"] = abs(row["rateDifferenceHz"]) <= thresholds["maximumVibratoRateDifferenceHz"]
            checks[f"vibratoExpectedRateA:{row['name']}"] = abs(row["vocalRackExpectedRateErrorHz"]) <= thresholds["maximumVibratoExpectedRateErrorHz"]
            checks[f"vibratoExpectedRateC:{row['name']}"] = abs(row["openUtauExpectedRateErrorHz"]) <= thresholds["maximumVibratoExpectedRateErrorHz"]
    for row in dynamics_rows:
        checks[f"dynamicsCorrelation:{row['name']}"] = row["bestLagCorrelation"] >= thresholds["minimumDynamicsContourCorrelation"]
        checks[f"dynamicsRmse:{row['name']}"] = row["alignedRmseDb"] <= thresholds["maximumDynamicsContourRmseDb"]
        checks[f"dynamicsLag:{row['name']}"] = abs(row["bestLagMsOpenUtauRelativeToVocalRack"]) <= thresholds["maximumDynamicsContourAbsoluteLagMs"]
        checks[f"dynamicsSpanA:{row['name']}"] = row["vocalRackSpanDb"] >= thresholds["minimumDynamicsSpanDb"]
        checks[f"dynamicsSpanC:{row['name']}"] = row["openUtauClassicSpanDb"] >= thresholds["minimumDynamicsSpanDb"]

    passed = all(checks.values())
    report = {"schemaVersion": 1, "sampleRate": vocal_rate, "targetHz": target_hz,
              "pitchSegments": pitch_rows, "dynamicsSegments": dynamics_rows,
              "thresholds": thresholds, "checks": checks, "passed": passed}
    (args.output_dir / "expression-report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    figure, axes = plt.subplots(2, len(pitch_series), figsize=(15, 6), sharey=True, constrained_layout=True)
    for column, (segment, grid, left, right) in enumerate(pitch_series):
        relative = grid - grid[0]
        axes[0, column].plot(relative, left, color="#d33f6a", lw=1.0)
        axes[1, column].plot(relative, right, color="#2b78d0", lw=1.0)
        axes[0, column].set_title(segment["name"])
        axes[1, column].set_xlabel("Time in window (s)")
        for axis in axes[:, column]:
            axis.grid(alpha=0.18)
    axes[0, 0].set_ylabel("VocalRack pitch (cents)")
    axes[1, 0].set_ylabel("OpenUtau Classic pitch (cents)")
    figure.suptitle("Adachi Rei expression baseline: separate pitch contours on one scale")
    figure.savefig(args.output_dir / "expression-pitch-contours.png", dpi=180)
    plt.close(figure)

    figure, axes = plt.subplots(2, 1, figsize=(12, 5), sharex=True, sharey=True, constrained_layout=True)
    segment, grid, left, right = dynamics_series[0]
    relative = grid - grid[0]
    axes[0].plot(relative, left, color="#d33f6a", lw=1.0)
    axes[1].plot(relative, right, color="#2b78d0", lw=1.0)
    axes[0].set_ylabel("VocalRack relative RMS (dB)")
    axes[1].set_ylabel("OpenUtau Classic relative RMS (dB)")
    axes[1].set_xlabel("Time in window (s)")
    for axis in axes:
        axis.grid(alpha=0.18)
    figure.suptitle("Adachi Rei authored dynamics: separate envelopes on one scale")
    figure.savefig(args.output_dir / "expression-dynamics-contours.png", dpi=180)
    plt.close(figure)

    print(json.dumps({"passed": passed, "pitchSegments": pitch_rows,
                      "dynamicsSegments": dynamics_rows, "checks": checks},
                     ensure_ascii=False, indent=2))
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
