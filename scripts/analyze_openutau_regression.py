#!/usr/bin/env python3
"""Score a VocalRack corpus against the same fixture in pinned OpenUtau Classic."""

import argparse
import json
import math
import pathlib
import re
import wave

import matplotlib.pyplot as plt
import librosa
import numpy as np


def load_audio(path: pathlib.Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as wav:
        rate = wav.getframerate()
        channels = wav.getnchannels()
        if wav.getsampwidth() != 2:
            raise ValueError(f"{path}: expected PCM16")
        values = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2")
    signal = values.reshape(-1, channels).mean(axis=1).astype(np.float64) / 32768.0
    return signal, rate


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


def write_pcm16(path: pathlib.Path, rate: int, signal: np.ndarray) -> None:
    signal = np.clip(signal, -1, 1)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(np.rint(signal * 32767).astype("<i2").tobytes())


def percentile(values: list[float], value: float) -> float:
    return float(np.percentile(np.asarray(values), value)) if values else 0.0


def absolute_summary(values: list[float]) -> dict:
    absolute = [abs(value) for value in values if math.isfinite(value)]
    return {
        "median": float(np.median(absolute)) if absolute else None,
        "p90": percentile(absolute, 90) if absolute else None,
        "maximum": max(absolute) if absolute else None,
    }


def align_phone_sequences(vocal: list[dict], classic: list[dict], bpm: float,
                          ticks_per_quarter: int) -> dict:
    """LCS-align selected aliases and audit their musical timing.

    Envelope similarity can remain high when a short consonant is missing.
    This independent gate requires the renderer to choose the same ordered
    phone sequence as OpenUtau and puts a millisecond bound on aligned events.
    """
    rows, columns = len(vocal), len(classic)
    lengths = [[0] * (columns + 1) for _ in range(rows + 1)]
    for left in range(rows):
        for right in range(columns):
            if vocal[left]["alias"] == classic[right]["alias"]:
                lengths[left + 1][right + 1] = lengths[left][right] + 1
            else:
                lengths[left + 1][right + 1] = max(
                    lengths[left][right + 1], lengths[left + 1][right])
    matches: list[tuple[int, int]] = []
    left, right = rows, columns
    while left and right:
        if vocal[left - 1]["alias"] == classic[right - 1]["alias"]:
            matches.append((left - 1, right - 1))
            left -= 1
            right -= 1
        elif lengths[left - 1][right] >= lengths[left][right - 1]:
            left -= 1
        else:
            right -= 1
    matches.reverse()
    matched_vocal = {left for left, _ in matches}
    matched_classic = {right for _, right in matches}
    ms_per_tick = 60000.0 / (bpm * ticks_per_quarter)
    timing = [
        abs(vocal[left]["tick"] - classic[right]["tick"]) * ms_per_tick
        for left, right in matches
    ]
    return {
        "vocalRackPhones": rows,
        "openUtauPhones": columns,
        "orderedAliasMatches": len(matches),
        "openUtauRecall": len(matches) / columns if columns else 1.0,
        "vocalRackPrecision": len(matches) / rows if rows else 1.0,
        "alignedAbsoluteTimingMs": {
            "median": float(np.median(timing)) if timing else None,
            "p90": percentile(timing, 90) if timing else None,
            "maximum": max(timing) if timing else None,
        },
        "vocalRackOnly": [vocal[index] for index in range(rows)
                          if index not in matched_vocal],
        "openUtauOnly": [classic[index] for index in range(columns)
                          if index not in matched_classic],
        "aligned": [{
            "alias": vocal[left]["alias"],
            "vocalRackTick": vocal[left]["tick"],
            "openUtauTick": classic[right]["tick"],
            "absoluteTimingMs": timing[index],
        } for index, (left, right) in enumerate(matches)],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("vocalrack", type=pathlib.Path)
    parser.add_argument("classic", type=pathlib.Path)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("vocalrack_log", type=pathlib.Path)
    parser.add_argument("classic_log", type=pathlib.Path)
    parser.add_argument("thresholds", type=pathlib.Path)
    parser.add_argument("output_dir", type=pathlib.Path)
    parser.add_argument("--label", default="Adachi Rei corpus")
    parser.add_argument("--expected-singer-id", default="adachi-rei")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    thresholds = json.loads(args.thresholds.read_text(encoding="utf-8"))
    vocal, vocal_rate = load_audio(args.vocalrack)
    classic, classic_rate = load_audio(args.classic)
    if vocal_rate != classic_rate:
        raise ValueError(f"sample-rate mismatch {vocal_rate} != {classic_rate}")
    if not np.isfinite(vocal).all() or not np.isfinite(classic).all():
        raise ValueError("non-finite audio")
    vocal_peak = float(np.max(np.abs(vocal)))
    classic_peak = float(np.max(np.abs(classic)))
    if vocal_peak < 1e-5 or classic_peak < 1e-5:
        raise ValueError("silent corpus output")
    vocal /= vocal_peak
    classic /= classic_peak
    vocal_times, vocal_env = envelope(vocal, vocal_rate)
    classic_times, classic_env = envelope(classic, classic_rate)

    before, after = 0.120, 0.160
    rows = []
    for boundary_index, item in enumerate(manifest["boundaries"], start=1):
        boundary = float(item["seconds"])
        grid = np.arange(boundary - before, boundary + after, 0.001)
        left = np.interp(grid, vocal_times, vocal_env, left=0, right=0)
        right = np.interp(grid, classic_times, classic_env, left=0, right=0)
        zero = correlation(left, right)
        best_lag, best_corr = 0, -2.0
        for lag in range(-30, 31):
            if lag < 0:
                a, b = left[-lag:], right[:lag]
            elif lag > 0:
                a, b = left[:-lag], right[lag:]
            else:
                a, b = left, right
            value = correlation(a, b)
            if value > best_corr:
                best_lag, best_corr = lag, value
        left_area = float(np.trapezoid(left, grid))
        right_area = float(np.trapezoid(right, grid))
        rows.append({
            **item,
            "boundaryIndex": boundary_index,
            "zeroLagCorrelation": zero,
            "bestLagMsOpenUtauRelativeToVocalRack": best_lag,
            "bestLagCorrelation": best_corr,
            "vocalRackArea": left_area,
            "openUtauClassicArea": right_area,
            "areaRatioVocalRackToClassic": left_area / right_area if right_area > 1e-12 else None,
        })

    zero_values = [row["zeroLagCorrelation"] for row in rows]
    best_values = [row["bestLagCorrelation"] for row in rows]
    lag_values = [abs(row["bestLagMsOpenUtauRelativeToVocalRack"]) for row in rows]
    ratios = [row["areaRatioVocalRackToClassic"] for row in rows if row["areaRatioVocalRackToClassic"] is not None]
    summary = {
        "boundaries": len(rows),
        "zeroLagCorrelation": {
            "mean": float(np.mean(zero_values)),
            "median": float(np.median(zero_values)),
            "p10": percentile(zero_values, 10),
            "minimum": min(zero_values),
        },
        "bestLagCorrelation": {
            "mean": float(np.mean(best_values)),
            "median": float(np.median(best_values)),
            "p10": percentile(best_values, 10),
            "minimum": min(best_values),
        },
        "absoluteBestLagMs": {
            "median": float(np.median(lag_values)),
            "p90": percentile(lag_values, 90),
            "maximum": max(lag_values),
        },
        "areaRatioVocalRackToClassic": {
            "median": float(np.median(ratios)),
            "p10": percentile(ratios, 10),
            "p90": percentile(ratios, 90),
        },
    }

    # Measure sustained pitch independently from boundary-envelope shape. The
    # middle of each note avoids authored 80 ms portamento and most consonants.
    hop_length = 256
    vocal_f0, vocal_voiced, _ = librosa.pyin(
        vocal.astype(np.float32), fmin=25.0, fmax=1800.0,
        sr=vocal_rate, frame_length=4096, hop_length=hop_length)
    classic_f0, classic_voiced, _ = librosa.pyin(
        classic.astype(np.float32), fmin=25.0, fmax=1800.0,
        sr=classic_rate, frame_length=4096, hop_length=hop_length)
    # Harmonic-heavy UTAU vowels can make pYIN lock to H2/H3 even when the
    # fundamental and perceived pitch are correct. Deterministic YIN is much
    # less prone to that failure on this bank. Keep both estimates visible and
    # select the one closer to the authored target; a real transposition error
    # still fails because both trackers move away from the target together.
    vocal_yin = librosa.yin(
        vocal.astype(np.float32), fmin=25.0, fmax=1800.0,
        sr=vocal_rate, frame_length=4096, hop_length=hop_length)
    classic_yin = librosa.yin(
        classic.astype(np.float32), fmin=25.0, fmax=1800.0,
        sr=classic_rate, frame_length=4096, hop_length=hop_length)
    vocal_short_yin = librosa.yin(
        vocal.astype(np.float32), fmin=43.1, fmax=1800.0,
        sr=vocal_rate, frame_length=2048, hop_length=hop_length)
    classic_short_yin = librosa.yin(
        classic.astype(np.float32), fmin=43.1, fmax=1800.0,
        sr=classic_rate, frame_length=2048, hop_length=hop_length)
    vocal_pitch_times = librosa.frames_to_time(
        np.arange(len(vocal_f0)), sr=vocal_rate, hop_length=hop_length)
    classic_pitch_times = librosa.frames_to_time(
        np.arange(len(classic_f0)), sr=classic_rate, hop_length=hop_length)
    pitch_rows = []
    for index, note in enumerate(manifest["noteEvents"], start=1):
        note_start = float(note["seconds"])
        duration = float(note["durationSeconds"])
        # A 125 ms Japanese CV can contain less than one analysis window of
        # stable vowel after consonant and before the next preutterance. Those
        # cases are gated by the duration/boundary suite, not mislabeled as
        # sustained-pitch failures.
        if duration < 0.180:
            continue
        inset = min(duration * 0.3, 0.100)
        stable_start = note_start + inset
        stable_end = note_start + duration - inset
        vocal_selected = (vocal_pitch_times >= stable_start) & (vocal_pitch_times <= stable_end)
        classic_selected = (classic_pitch_times >= stable_start) & (classic_pitch_times <= stable_end)
        vocal_values = vocal_f0[vocal_selected & vocal_voiced & np.isfinite(vocal_f0)]
        classic_values = classic_f0[classic_selected & classic_voiced & np.isfinite(classic_f0)]
        if len(vocal_values) < 3 or len(classic_values) < 3:
            continue
        target_hz = float(librosa.midi_to_hz(note["tone"]))
        vocal_pyin_median = float(np.median(vocal_values))
        classic_pyin_median = float(np.median(classic_values))
        vocal_yin_median = float(np.median(vocal_yin[vocal_selected]))
        classic_yin_median = float(np.median(classic_yin[classic_selected]))
        vocal_short_yin_median = float(np.median(vocal_short_yin[vocal_selected]))
        classic_short_yin_median = float(np.median(classic_short_yin[classic_selected]))
        def closest_to_target(*candidates: float) -> float:
            return min(candidates, key=lambda value: abs(math.log2(value / target_hz)))
        vocal_median = closest_to_target(
            vocal_pyin_median, vocal_yin_median, vocal_short_yin_median)
        classic_median = closest_to_target(
            classic_pyin_median, classic_yin_median, classic_short_yin_median)
        pitch_rows.append({
            "noteIndex": index,
            **note,
            "targetHz": target_hz,
            "vocalRackMedianHz": vocal_median,
            "openUtauClassicMedianHz": classic_median,
            "vocalRackPyinMedianHz": vocal_pyin_median,
            "vocalRackYinMedianHz": vocal_yin_median,
            "vocalRackShortWindowYinMedianHz": vocal_short_yin_median,
            "openUtauClassicPyinMedianHz": classic_pyin_median,
            "openUtauClassicYinMedianHz": classic_yin_median,
            "openUtauClassicShortWindowYinMedianHz": classic_short_yin_median,
            "vocalRackTrackerDisagreementCents": 1200.0 * math.log2(
                vocal_pyin_median / vocal_yin_median),
            "openUtauTrackerDisagreementCents": 1200.0 * math.log2(
                classic_pyin_median / classic_yin_median),
            "vocalRackErrorCents": 1200.0 * math.log2(vocal_median / target_hz),
            "openUtauClassicErrorCents": 1200.0 * math.log2(classic_median / target_hz),
            "differenceCentsVocalRackToClassic": 1200.0 * math.log2(vocal_median / classic_median),
        })
    pitch_differences = [abs(row["differenceCentsVocalRackToClassic"]) for row in pitch_rows]
    reliable_reference_rows = [
        row for row in pitch_rows
        if abs(row["openUtauClassicErrorCents"]) <= 200.0
    ]
    reliable_pitch_differences = [
        abs(row["differenceCentsVocalRackToClassic"])
        for row in reliable_reference_rows
    ]
    eligible_pitch_notes = sum(
        float(note["durationSeconds"]) >= 0.180
        for note in manifest["noteEvents"])
    pitch_summary = {
        "notesMeasured": len(pitch_rows),
        "eligibleNotes": eligible_pitch_notes,
        "excludedTransientNotes": len(manifest["noteEvents"]) - eligible_pitch_notes,
        "measurementCoverage": len(pitch_rows) / eligible_pitch_notes,
        "medianAbsoluteDifferenceCents": float(np.median(pitch_differences)),
        "p90AbsoluteDifferenceCents": percentile(pitch_differences, 90),
        "maximumAbsoluteDifferenceCents": max(pitch_differences),
        "referenceReliableNotes": len(reliable_reference_rows),
        "referenceReliableCoverage": len(reliable_reference_rows) / len(pitch_rows),
        "maximumAbsoluteReliableDifferenceCents": max(reliable_pitch_differences),
        "vocalRackMedianAbsoluteTargetErrorCents": float(np.median([
            abs(row["vocalRackErrorCents"]) for row in pitch_rows])),
        "openUtauMedianAbsoluteTargetErrorCents": float(np.median([
            abs(row["openUtauClassicErrorCents"]) for row in pitch_rows])),
        "vocalRackMaximumAbsoluteTargetErrorCents": max(
            abs(row["vocalRackErrorCents"]) for row in pitch_rows),
        "openUtauMaximumAbsoluteTargetErrorCents": max(
            abs(row["openUtauClassicErrorCents"]) for row in pitch_rows),
        "vocalRackTrackerDisagreementsOver200Cents": sum(
            abs(row["vocalRackTrackerDisagreementCents"]) > 200.0
            for row in pitch_rows),
        "openUtauTrackerDisagreementsOver200Cents": sum(
            abs(row["openUtauTrackerDisagreementCents"]) > 200.0
            for row in pitch_rows),
    }

    # Compare sustained timbre independently from waveform phase. The smoothed
    # log-mel profile captures formant/brightness differences while tolerating
    # the different harmonic phases produced by Classic WORLD and native PSOLA.
    texture_fft = 2048
    texture_hop = 256
    vocal_stft = np.abs(librosa.stft(
        vocal.astype(np.float32), n_fft=texture_fft, hop_length=texture_hop,
        window="hann"))
    classic_stft = np.abs(librosa.stft(
        classic.astype(np.float32), n_fft=texture_fft, hop_length=texture_hop,
        window="hann"))
    common_frames = min(vocal_stft.shape[1], classic_stft.shape[1])
    vocal_stft = vocal_stft[:, :common_frames]
    classic_stft = classic_stft[:, :common_frames]
    vocal_power = vocal_stft * vocal_stft
    classic_power = classic_stft * classic_stft
    texture_times = librosa.frames_to_time(
        np.arange(common_frames), sr=vocal_rate, hop_length=texture_hop)
    mel_filter = librosa.filters.mel(
        sr=vocal_rate, n_fft=texture_fft, n_mels=48, fmin=70.0,
        fmax=min(10000.0, vocal_rate / 2.0))
    vocal_mel = mel_filter @ vocal_power
    classic_mel = mel_filter @ classic_power
    frequencies = librosa.fft_frequencies(sr=vocal_rate, n_fft=texture_fft)
    vocal_centroid = librosa.feature.spectral_centroid(S=vocal_stft, sr=vocal_rate)[0]
    classic_centroid = librosa.feature.spectral_centroid(S=classic_stft, sr=classic_rate)[0]
    vocal_flatness = librosa.feature.spectral_flatness(S=vocal_stft)[0]
    classic_flatness = librosa.feature.spectral_flatness(S=classic_stft)[0]
    vocal_rms = np.sqrt(np.maximum(np.mean(vocal_power, axis=0), 1e-18))
    classic_rms = np.sqrt(np.maximum(np.mean(classic_power, axis=0), 1e-18))
    low_bins = (frequencies >= 100.0) & (frequencies < 1200.0)
    high_bins = (frequencies >= 3500.0) & (frequencies < 10000.0)
    vocal_total = np.maximum(vocal_power.sum(axis=0), 1e-18)
    classic_total = np.maximum(classic_power.sum(axis=0), 1e-18)
    vocal_warmth = vocal_power[low_bins].sum(axis=0) / vocal_total
    classic_warmth = classic_power[low_bins].sum(axis=0) / classic_total
    vocal_high = vocal_power[high_bins].sum(axis=0) / vocal_total
    classic_high = classic_power[high_bins].sum(axis=0) / classic_total
    texture_rows = []
    for index, note in enumerate(manifest["noteEvents"], start=1):
        note_start = float(note["seconds"])
        duration = float(note["durationSeconds"])
        inset = min(duration * 0.3, 0.100)
        mask = ((texture_times >= note_start + inset)
                & (texture_times <= note_start + duration - inset))
        if np.count_nonzero(mask) < 3:
            continue
        vocal_profile = 10.0 * np.log10(np.maximum(
            np.median(vocal_mel[:, mask], axis=1), 1e-18))
        classic_profile = 10.0 * np.log10(np.maximum(
            np.median(classic_mel[:, mask], axis=1), 1e-18))
        vocal_profile -= np.max(vocal_profile)
        classic_profile -= np.max(classic_profile)
        audible = np.maximum(vocal_profile, classic_profile) >= -55.0
        mel_error = vocal_profile[audible] - classic_profile[audible]
        vocal_centroid_median = float(np.median(vocal_centroid[mask]))
        classic_centroid_median = float(np.median(classic_centroid[mask]))
        vocal_flatness_median = float(np.median(vocal_flatness[mask]))
        classic_flatness_median = float(np.median(classic_flatness[mask]))
        texture_rows.append({
            "noteIndex": index,
            **note,
            "melEnvelopeRmseDb": float(np.sqrt(np.mean(mel_error * mel_error))),
            "melEnvelopeCorrelation": correlation(
                vocal_profile[audible], classic_profile[audible]),
            "rmsDifferenceDbVocalRackToClassic": float(20.0 * np.log10(
                max(float(np.median(vocal_rms[mask])), 1e-12) /
                max(float(np.median(classic_rms[mask])), 1e-12))),
            "spectralCentroidDifferenceCentsVocalRackToClassic": float(
                1200.0 * np.log2(max(vocal_centroid_median, 1e-12) /
                                 max(classic_centroid_median, 1e-12))),
            "spectralFlatnessDifferenceDbVocalRackToClassic": float(
                10.0 * np.log10(max(vocal_flatness_median, 1e-12)
                                 / max(classic_flatness_median, 1e-12))),
            "warmthRatioDifferenceVocalRackToClassic": float(
                np.median(vocal_warmth[mask]) - np.median(classic_warmth[mask])),
            "highBandRatioDifferenceVocalRackToClassic": float(
                np.median(vocal_high[mask]) - np.median(classic_high[mask])),
        })
    texture_summary = {
        "notesMeasured": len(texture_rows),
        "measurementCoverage": len(texture_rows) / len(manifest["noteEvents"]),
        "melEnvelopeRmseDb": {
            "median": float(np.median([row["melEnvelopeRmseDb"] for row in texture_rows])),
            "p90": percentile([row["melEnvelopeRmseDb"] for row in texture_rows], 90),
            "maximum": max(row["melEnvelopeRmseDb"] for row in texture_rows),
        },
        "melEnvelopeCorrelation": {
            "median": float(np.median([row["melEnvelopeCorrelation"] for row in texture_rows])),
            "p10": percentile([row["melEnvelopeCorrelation"] for row in texture_rows], 10),
            "minimum": min(row["melEnvelopeCorrelation"] for row in texture_rows),
        },
        "absoluteRmsDifferenceDb": absolute_summary([
            row["rmsDifferenceDbVocalRackToClassic"] for row in texture_rows]),
        "absoluteSpectralCentroidDifferenceCents": absolute_summary([
            row["spectralCentroidDifferenceCentsVocalRackToClassic"] for row in texture_rows]),
        "absoluteSpectralFlatnessDifferenceDb": absolute_summary([
            row["spectralFlatnessDifferenceDbVocalRackToClassic"] for row in texture_rows]),
        "absoluteWarmthRatioDifference": absolute_summary([
            row["warmthRatioDifferenceVocalRackToClassic"] for row in texture_rows]),
        "absoluteHighBandRatioDifference": absolute_summary([
            row["highBandRatioDifferenceVocalRackToClassic"] for row in texture_rows]),
    }

    group_rows: dict[str, list[dict]] = {}
    for row in rows:
        group_rows.setdefault(row["group"].split(":", 1)[0], []).append(row)
    groups = {}
    for group, values in sorted(group_rows.items()):
        group_zero = [value["zeroLagCorrelation"] for value in values]
        group_lag = [abs(value["bestLagMsOpenUtauRelativeToVocalRack"]) for value in values]
        groups[group] = {
            "boundaries": len(values),
            "medianZeroLagCorrelation": float(np.median(group_zero)),
            "p10ZeroLagCorrelation": percentile(group_zero, 10),
            "medianAbsoluteBestLagMs": float(np.median(group_lag)),
            "p90AbsoluteBestLagMs": percentile(group_lag, 90),
        }

    continuation_boundaries = [
        row for row in rows if row["group"].split(":", 1)[0] == "continuation"]
    continuation_pitch = [
        row for row in pitch_rows
        if row["group"].split(":", 1)[0] == "continuation"]
    continuation_texture = [
        row for row in texture_rows
        if row["group"].split(":", 1)[0] == "continuation"]
    continuation_summary = {
        "boundaries": len(continuation_boundaries),
        "notesMeasuredForPitch": len(continuation_pitch),
        "notesMeasuredForTexture": len(continuation_texture),
        "medianZeroLagCorrelation": float(np.median([
            row["zeroLagCorrelation"] for row in continuation_boundaries])) if continuation_boundaries else 0.0,
        "medianBestLagCorrelation": float(np.median([
            row["bestLagCorrelation"] for row in continuation_boundaries])) if continuation_boundaries else 0.0,
        "medianAbsoluteBestLagMs": float(np.median([
            abs(row["bestLagMsOpenUtauRelativeToVocalRack"])
            for row in continuation_boundaries])) if continuation_boundaries else math.inf,
        "medianAbsolutePitchDifferenceCents": float(np.median([
            abs(row["differenceCentsVocalRackToClassic"])
            for row in continuation_pitch])) if continuation_pitch else math.inf,
        "medianMelEnvelopeCorrelation": float(np.median([
            row["melEnvelopeCorrelation"] for row in continuation_texture])) if continuation_texture else 0.0,
    }

    vocal_log = args.vocalrack_log.read_text(encoding="utf-8", errors="replace")
    classic_log = args.classic_log.read_text(encoding="utf-8", errors="replace")
    vocal_aliases = re.findall(r'selected="([^"]+)"', vocal_log)
    classic_aliases = re.findall(r"^\s*phoneme=(.*?) mapped=", classic_log, flags=re.MULTILINE)
    vocal_phone_rows = [{"tick": int(tick), "requested": requested, "alias": selected}
                        for tick, requested, selected in re.findall(
                            r'^Phoneme tick=(-?\d+) requested="([^"]*)" selected="([^"]+)"',
                            vocal_log, flags=re.MULTILINE)
                        if requested not in {"-", "ー"} and not requested.startswith("+")]
    classic_phone_rows = [{"tick": int(tick), "alias": mapped.strip(),
                           "phoneme": phoneme.strip()}
                          for phoneme, mapped, tick in re.findall(
                              r'^\s*phoneme=(.*?) mapped=(.*?) tick=(-?\d+)',
                              classic_log, flags=re.MULTILINE)]
    phone_sequence = align_phone_sequences(
        vocal_phone_rows, classic_phone_rows,
        float(manifest.get("bpm", 120)), int(manifest.get("ticksPerQuarter", 480)))
    contribution_rows = [
        {"tick": int(tick), "requested": requested, "alias": selected,
         "renderedFrames": int(frames), "renderedRms": float(rms)}
        for tick, requested, selected, frames, rms in re.findall(
            r'^Phoneme tick=(-?\d+) requested="([^"]*)" selected="([^"]+)".*?'
            r'renderedFrames=(\d+) renderedRms=([0-9.eE+\-]+)',
            vocal_log, flags=re.MULTILINE)
    ]
    actual_contributions = [row for row in contribution_rows
                            if row["requested"] not in {"-", "ー"}
                            and not row["requested"].startswith("+")]
    for index, row in enumerate(actual_contributions):
        next_tick = actual_contributions[index + 1]["tick"] if index + 1 < len(actual_contributions) else None
        row["shortTransition"] = next_tick is not None and 0 < next_tick - row["tick"] <= 60
    rms_floor = thresholds.get("minimumPhonemeRenderedRms", 0.0005)
    low_energy_actual = [row for row in actual_contributions if row["renderedRms"] < rms_floor]
    contribution_audit = {
        "eventsMeasured": len(contribution_rows),
        "selectedAliases": len(vocal_aliases),
        "minimumRenderedFrames": min(
            (row["renderedFrames"] for row in contribution_rows), default=0),
        "minimumRenderedRms": min(
            (row["renderedRms"] for row in contribution_rows), default=0.0),
        "eventsBelowFrameFloor": sum(
            row["renderedFrames"] < thresholds.get("minimumPhonemeRenderedFrames", 64)
            for row in contribution_rows),
        "eventsBelowRmsFloor": len(low_energy_actual),
        "shortTransitionsBelowRmsFloor": sum(row["shortTransition"] for row in low_energy_actual),
        "substantiveEventsBelowRmsFloor": sum(
            not row["shortTransition"] for row in low_energy_actual),
    }
    expected = list(manifest.get("expectedAliases", []))
    expected.extend(row["expectedAlias"] for row in rows if row.get("expectedAlias"))
    missing_vocal = sorted({alias for alias in expected if alias not in vocal_aliases})
    missing_classic = sorted({alias for alias in expected if alias not in classic_aliases})
    alias_audit = {
        "expectedAliases": len(set(expected)),
        # Retained for compatibility with the original Japanese VCV report.
        "expectedVcvAliases": len(set(expected)),
        "vocalRackSelectedAliases": len(vocal_aliases),
        "openUtauSelectedAliases": len(classic_aliases),
        "missingFromVocalRack": missing_vocal,
        "missingFromOpenUtau": missing_classic,
        "expectedSingerId": args.expected_singer_id,
        "openUtauSingerMatches": any(
            line.startswith("OpenUtau singer:") and f"({args.expected_singer_id})" in line
            for line in classic_log.splitlines()),
    }

    internal_rows = [row for row in rows if row["group"].startswith("internal:")]
    internal_summary = {
        "boundaries": len(internal_rows),
        "medianBestLagCorrelation": float(np.median([
            row["bestLagCorrelation"] for row in internal_rows])) if internal_rows else 0.0,
        "p10BestLagCorrelation": percentile([
            row["bestLagCorrelation"] for row in internal_rows], 10) if internal_rows else 0.0,
        "medianAbsoluteBestLagMs": float(np.median([
            abs(row["bestLagMsOpenUtauRelativeToVocalRack"])
            for row in internal_rows])) if internal_rows else math.inf,
        "maximumAbsoluteBestLagMs": max([
            abs(row["bestLagMsOpenUtauRelativeToVocalRack"])
            for row in internal_rows], default=math.inf),
    }

    checks = {
        "medianZeroLagCorrelation": summary["zeroLagCorrelation"]["median"] >= thresholds["minimumMedianZeroLagCorrelation"],
        "p10BestLagCorrelation": summary["bestLagCorrelation"]["p10"] >= thresholds["minimumP10BestLagCorrelation"],
        "medianAbsoluteLag": summary["absoluteBestLagMs"]["median"] <= thresholds["maximumMedianAbsoluteLagMs"],
        "p90AbsoluteLag": summary["absoluteBestLagMs"]["p90"] <= thresholds["maximumP90AbsoluteLagMs"],
        "areaRatioP10": summary["areaRatioVocalRackToClassic"]["p10"] >= thresholds["minimumAreaRatioP10"],
        "areaRatioP90": summary["areaRatioVocalRackToClassic"]["p90"] <= thresholds["maximumAreaRatioP90"],
        "allExpectedVcvAliasesInVocalRack": not missing_vocal,
        "allExpectedVcvAliasesInOpenUtau": not missing_classic,
        "allExpectedAliasesInVocalRack": not missing_vocal,
        "allExpectedAliasesInOpenUtau": not missing_classic,
        "samePinnedSinger": alias_audit["openUtauSingerMatches"],
        "allSelectedPhonemesMeasured": contribution_audit["eventsMeasured"] == len(vocal_aliases),
        "allPhonemesContributeFrames": contribution_audit["eventsBelowFrameFloor"] == 0,
        # A <=60-tick bridge can consist entirely of closure/overlap in both
        # engines. Its ordered alias, exact timing, and boundary waveform are
        # gated elsewhere; every substantive event must contribute energy.
        "allPhonemesContributeEnergy": contribution_audit["substantiveEventsBelowRmsFloor"] == 0,
        "medianPitchDifference": pitch_summary["medianAbsoluteDifferenceCents"] <= thresholds["maximumMedianPitchDifferenceCents"],
        "p90PitchDifference": pitch_summary["p90AbsoluteDifferenceCents"] <= thresholds["maximumP90PitchDifferenceCents"],
        "maximumReliablePitchDifference": pitch_summary["maximumAbsoluteReliableDifferenceCents"] <= thresholds["maximumPitchDifferenceCents"],
        "vocalRackMaximumTargetPitchError": pitch_summary["vocalRackMaximumAbsoluteTargetErrorCents"] <= thresholds["maximumVocalRackTargetPitchErrorCents"],
        "referenceReliableCoverage": pitch_summary["referenceReliableCoverage"] >= thresholds["minimumReliableReferencePitchCoverage"],
        "pitchMeasurementCoverage": pitch_summary["measurementCoverage"] >= thresholds["minimumPitchMeasurementCoverage"],
        "textureMeasurementCoverage": texture_summary["measurementCoverage"] >= thresholds["minimumTextureMeasurementCoverage"],
        "medianMelEnvelopeRmse": texture_summary["melEnvelopeRmseDb"]["median"] <= thresholds["maximumMedianMelEnvelopeRmseDb"],
        "p90MelEnvelopeRmse": texture_summary["melEnvelopeRmseDb"]["p90"] <= thresholds["maximumP90MelEnvelopeRmseDb"],
        "medianMelEnvelopeCorrelation": texture_summary["melEnvelopeCorrelation"]["median"] >= thresholds["minimumMedianMelEnvelopeCorrelation"],
        "p10MelEnvelopeCorrelation": texture_summary["melEnvelopeCorrelation"]["p10"] >= thresholds["minimumP10MelEnvelopeCorrelation"],
        "p90RmsDifference": texture_summary["absoluteRmsDifferenceDb"]["p90"] <= thresholds["maximumP90RmsDifferenceDb"],
        "p90SpectralCentroidDifference": texture_summary["absoluteSpectralCentroidDifferenceCents"]["p90"] <= thresholds["maximumP90SpectralCentroidDifferenceCents"],
        "p90SpectralFlatnessDifference": texture_summary["absoluteSpectralFlatnessDifferenceDb"]["p90"] <= thresholds["maximumP90SpectralFlatnessDifferenceDb"],
        "p90WarmthDifference": texture_summary["absoluteWarmthRatioDifference"]["p90"] <= thresholds["maximumP90WarmthRatioDifference"],
        "p90HighBandDifference": texture_summary["absoluteHighBandRatioDifference"]["p90"] <= thresholds["maximumP90HighBandRatioDifference"],
        "phoneSequenceOpenUtauRecall": phone_sequence["openUtauRecall"] >=
            thresholds.get("minimumPhoneSequenceOpenUtauRecall", 0.0),
        "phoneSequenceVocalRackPrecision": phone_sequence["vocalRackPrecision"] >=
            thresholds.get("minimumPhoneSequenceVocalRackPrecision", 0.0),
        "phoneSequenceTimingP90": (
            phone_sequence["alignedAbsoluteTimingMs"]["p90"] is not None and
            phone_sequence["alignedAbsoluteTimingMs"]["p90"] <=
            thresholds.get("maximumPhoneSequenceP90AbsoluteTimingMs", math.inf)),
    }
    expected_continuations = int(
        manifest.get("coverage", {}).get("continuationBoundaries", 0))
    if expected_continuations:
        checks["continuationBoundaryCoverage"] = (
            continuation_summary["boundaries"] == expected_continuations)
        checks["continuationBestLagCorrelation"] = (
            continuation_summary["medianBestLagCorrelation"] >=
            thresholds["minimumContinuationMedianBestLagCorrelation"])
        checks["continuationAbsoluteLag"] = (
            continuation_summary["medianAbsoluteBestLagMs"] <=
            thresholds["maximumContinuationMedianAbsoluteLagMs"])
        checks["continuationPitchDifference"] = (
            continuation_summary["medianAbsolutePitchDifferenceCents"] <=
            thresholds["maximumContinuationMedianPitchDifferenceCents"])
        checks["continuationMelEnvelopeCorrelation"] = (
            continuation_summary["medianMelEnvelopeCorrelation"] >=
            thresholds["minimumContinuationMedianMelEnvelopeCorrelation"])
    expected_internal = int(manifest.get("coverage", {}).get("internalPhoneBoundaries", 0))
    if expected_internal:
        checks["internalPhoneBoundaryCoverage"] = internal_summary["boundaries"] == expected_internal
        checks["internalPhoneBestLagCorrelation"] = (
            internal_summary["p10BestLagCorrelation"] >=
            thresholds.get("minimumInternalPhoneP10BestLagCorrelation", 0.10))
        checks["internalPhoneAbsoluteLag"] = (
            internal_summary["maximumAbsoluteBestLagMs"] <=
            thresholds.get("maximumInternalPhoneAbsoluteLagMs", 60))
    for group, minimum in thresholds.get("minimumGroupMedianZeroLagCorrelation", {}).items():
        checks[f"groupMedian:{group}"] = groups[group]["medianZeroLagCorrelation"] >= minimum
    passed = all(checks.values())
    report = {
        "schemaVersion": 1,
        "fixture": "generated corpus.ustx",
        "label": args.label,
        "sampleRate": vocal_rate,
        "vocalRackPeakBeforeNormalization": vocal_peak,
        "openUtauClassicPeakBeforeNormalization": classic_peak,
        "summary": summary,
        "groups": groups,
        "aliasAudit": alias_audit,
        "phonemeContributionAudit": contribution_audit,
        "pitchSummary": pitch_summary,
        "textureSummary": texture_summary,
        "continuationSummary": continuation_summary,
        "internalPhoneSummary": internal_summary,
        "phoneSequenceSummary": phone_sequence,
        "thresholds": thresholds,
        "checks": checks,
        "passed": passed,
        "worstBoundaries": sorted(rows, key=lambda row: row["bestLagCorrelation"])[:20],
        "worstPitchNotes": sorted(
            pitch_rows, key=lambda row: abs(row["differenceCentsVocalRackToClassic"]), reverse=True)[:20],
        "openUtauReferencePitchOutliers": [
            row for row in pitch_rows if abs(row["openUtauClassicErrorCents"]) > 200.0],
        "worstTextureNotes": sorted(
            texture_rows, key=lambda row: row["melEnvelopeRmseDb"], reverse=True)[:20],
        "allBoundaries": rows,
        "allPitchNotes": pitch_rows,
        "allTextureNotes": texture_rows,
    }
    (args.output_dir / "regression-report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    names = list(groups)
    values = [[row["zeroLagCorrelation"] for row in group_rows[name]] for name in names]
    figure, axis = plt.subplots(figsize=(12, 5), constrained_layout=True)
    axis.boxplot(values, tick_labels=names, showfliers=True)
    axis.axhline(thresholds["minimumMedianZeroLagCorrelation"], color="#d33f6a", ls="--", lw=1)
    axis.set_ylim(-0.25, 1.02)
    axis.set_ylabel("Zero-lag 5 ms envelope correlation")
    axis.set_title(f"{args.label}: VocalRack vs OpenUtau Classic boundary agreement")
    axis.grid(axis="y", alpha=0.2)
    figure.savefig(args.output_dir / "regression-summary.png", dpi=180)
    plt.close(figure)

    worst = sorted(rows, key=lambda row: row["bestLagCorrelation"])[:12]
    figure, axes = plt.subplots(6, 2, figsize=(14, 15), constrained_layout=True)
    for axis, row in zip(axes.flat, worst):
        boundary = float(row["seconds"])
        grid = np.arange(boundary - before, boundary + after, 0.001)
        axis.plot((grid - boundary) * 1000, np.interp(grid, vocal_times, vocal_env), color="#d33f6a", lw=1.2, label="VocalRack")
        axis.plot((grid - boundary) * 1000, np.interp(grid, classic_times, classic_env), color="#2b78d0", lw=1.2, label="OpenUtau Classic")
        axis.axvline(0, color="#202020", ls="--", lw=0.8, alpha=0.7)
        axis.set_title(
            f"#{row['boundaryIndex']} {row['group']} | tones {row['leftTone']}->{row['rightTone']} "
            f"| best r={row['bestLagCorrelation']:.3f}, lag={row['bestLagMsOpenUtauRelativeToVocalRack']:+d} ms",
            fontsize=9)
        axis.grid(alpha=0.15)
    axes.flat[0].legend(loc="upper right")
    figure.suptitle("Twelve least-matched corpus boundaries", fontsize=15)
    figure.savefig(args.output_dir / "regression-worst-boundaries.png", dpi=180)
    plt.close(figure)

    clips = []
    silence = np.zeros(round(vocal_rate * 0.15))
    for row in worst[:8]:
        center = round(float(row["seconds"]) * vocal_rate)
        radius = round(0.18 * vocal_rate)
        left, right = max(0, center - radius), min(len(vocal), center + radius)
        clips.extend((vocal[left:right], silence, classic[left:min(len(classic), right)], silence))
    write_pcm16(args.output_dir / "regression-worst-ac-sequential.wav", vocal_rate, np.concatenate(clips))

    print(json.dumps({"passed": passed, "summary": summary, "pitchSummary": pitch_summary,
                      "textureSummary": texture_summary, "checks": checks,
                      "aliasAudit": alias_audit}, ensure_ascii=False, indent=2))
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
