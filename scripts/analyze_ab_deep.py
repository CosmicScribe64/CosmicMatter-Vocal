#!/usr/bin/env python3
"""Acoustic, note-aligned VocalRack/OpenUtau comparison with visual evidence."""

import json
import math
import pathlib
import sys

import librosa
import matplotlib.pyplot as plt
import numpy as np
import yaml
from scipy.signal import find_peaks


def load_mono(path: pathlib.Path) -> tuple[np.ndarray, int]:
    signal, rate = librosa.load(path, sr=None, mono=True)
    return signal.astype(np.float64), int(rate)


def notes_from_ustx(path: pathlib.Path) -> tuple[float, list[dict]]:
    project = yaml.safe_load(path.read_text(encoding="utf-8"))
    bpm = float(project["tempos"][0]["bpm"])
    notes = []
    for part in project["voice_parts"]:
        base = int(part.get("position", 0))
        for note in part["notes"]:
            start_tick = base + int(note["position"])
            duration_tick = int(note["duration"])
            notes.append({
                "lyric": str(note["lyric"]),
                "midi": int(note["tone"]),
                "start": start_tick * 60.0 / (bpm * 480.0),
                "end": (start_tick + duration_tick) * 60.0 / (bpm * 480.0),
            })
    return bpm, sorted(notes, key=lambda note: note["start"])


def safe_stats(values: np.ndarray) -> dict:
    values = values[np.isfinite(values)]
    if not len(values):
        return {"median": None, "p10": None, "p90": None}
    return {
        "median": float(np.median(values)),
        "p10": float(np.percentile(values, 10)),
        "p90": float(np.percentile(values, 90)),
    }


def analyze(signal: np.ndarray, rate: int, notes: list[dict]) -> tuple[dict, dict]:
    frame_length, hop = 4096, 256
    f0, voiced, voiced_probability = librosa.pyin(
        signal, fmin=65.0, fmax=700.0, sr=rate, frame_length=frame_length, hop_length=hop)
    stft = np.abs(librosa.stft(signal, n_fft=frame_length, hop_length=hop, window="hann"))
    power = stft * stft
    frequencies = librosa.fft_frequencies(sr=rate, n_fft=frame_length)
    times = librosa.frames_to_time(np.arange(stft.shape[1]), sr=rate, hop_length=hop)
    rms = librosa.feature.rms(S=stft, frame_length=frame_length, hop_length=hop)[0]
    centroid = librosa.feature.spectral_centroid(S=stft, sr=rate)[0]
    flatness = librosa.feature.spectral_flatness(S=stft)[0]
    low = power[(frequencies >= 100) & (frequencies < 1200)].sum(axis=0)
    high = power[(frequencies >= 3500) & (frequencies < 10000)].sum(axis=0)
    total = np.maximum(power.sum(axis=0), 1e-18)
    warmth = low / total
    high_ratio = high / total
    active = rms > max(1e-5, float(np.max(rms)) * 0.025)
    rms_db = 20.0 * np.log10(np.maximum(rms, 1e-8))
    note_results = []
    for note in notes:
        # Avoid consonant attacks and release tails; include more of sustained notes.
        inset = min(0.16, max(0.06, (note["end"] - note["start"]) * 0.28))
        mask = (times >= note["start"] + inset) & (times <= note["end"] - 0.06) & active
        pitches = f0[mask & voiced & np.isfinite(f0)]
        target = 440.0 * 2.0 ** ((note["midi"] - 69) / 12.0)
        cents = 1200.0 * np.log2(pitches / target) if len(pitches) else np.array([])
        local_rms = rms[mask]
        median_rms = float(np.median(local_rms)) if len(local_rms) else 0.0
        note_results.append({
            **note,
            "targetHz": target,
            "f0Hz": safe_stats(pitches),
            "centsError": safe_stats(cents),
            "voicedFraction": float(np.mean(voiced[mask])) if np.any(mask) else 0.0,
            "octaveErrorFraction": float(np.mean(np.abs(cents) > 700.0)) if len(cents) else 1.0,
            "rmsDbMedian": float(20.0 * math.log10(max(median_rms, 1e-8))),
            "rmsDropoutFraction": float(np.mean(local_rms < median_rms * 0.25)) if len(local_rms) else 1.0,
            "spectralCentroidHz": safe_stats(centroid[mask]),
            "spectralFlatness": safe_stats(flatness[mask]),
            "warmthRatio": safe_stats(warmth[mask]),
            "highBandRatio": safe_stats(high_ratio[mask]),
        })
    summary = {
        "durationSeconds": len(signal) / rate,
        "sampleRate": rate,
        "rmsDb": float(20.0 * math.log10(max(float(np.sqrt(np.mean(signal * signal))), 1e-8))),
        "peak": float(np.max(np.abs(signal))),
        "activeSpectralCentroidHz": safe_stats(centroid[active]),
        "activeSpectralFlatness": safe_stats(flatness[active]),
        "activeWarmthRatio": safe_stats(warmth[active]),
        "activeHighBandRatio": safe_stats(high_ratio[active]),
        "activeRmsP10Db": float(np.percentile(rms_db[active], 10)),
        "activeRmsP90Db": float(np.percentile(rms_db[active], 90)),
        "notes": note_results,
    }
    tracks = {
        "times": times,
        "f0": f0,
        "voiced": voiced,
        "voicedProbability": voiced_probability,
        "rmsDb": rms_db,
        "centroid": centroid,
        "flatness": flatness,
        "warmth": warmth,
        "highRatio": high_ratio,
    }
    return summary, tracks


def rounded(value):
    if value is None:
        return None
    if isinstance(value, float):
        return round(value, 6)
    if isinstance(value, dict):
        return {key: rounded(item) for key, item in value.items()}
    if isinstance(value, list):
        return [rounded(item) for item in value]
    return value


def plot(out: pathlib.Path, notes: list[dict], signals: dict, tracks: dict, comparison_label: str) -> None:
    colors = {"VocalRack": "#d33f6a", comparison_label: "#2b78d0"}
    fig = plt.figure(figsize=(16, 14), constrained_layout=True)
    grid = fig.add_gridspec(5, 2, height_ratios=[1.25, 1, 1, 1, 1])
    vocal_wave = fig.add_subplot(grid[0, 0])
    open_wave = fig.add_subplot(grid[0, 1], sharex=vocal_wave, sharey=vocal_wave)
    pitch_axis = fig.add_subplot(grid[1, :], sharex=vocal_wave)
    loudness_axis = fig.add_subplot(grid[2, :], sharex=vocal_wave)
    brightness_axis = fig.add_subplot(grid[3, :], sharex=vocal_wave)
    stability_axis = fig.add_subplot(grid[4, :], sharex=vocal_wave)
    axes = [vocal_wave, open_wave, pitch_axis, loudness_axis, brightness_axis, stability_axis]
    duration = max(len(sig) / rate for sig, rate in signals.values())
    peak = max(float(np.max(np.abs(signal))) for signal, _ in signals.values())
    for lane, (name, (signal, rate)) in enumerate(signals.items()):
        stride = max(1, len(signal) // 6000)
        x = np.arange(0, len(signal), stride) / rate
        data = tracks[name]
        axes[lane].plot(x, signal[::stride], lw=0.45, alpha=0.72, color=colors[name])
        frame_rms = 10.0 ** (data["rmsDb"] / 20.0)
        axes[lane].plot(data["times"], frame_rms, lw=1.35, color="#202020", alpha=0.85, label="RMS envelope")
        axes[lane].plot(data["times"], -frame_rms, lw=1.35, color="#202020", alpha=0.85)
        axes[lane].set_ylim(-peak * 1.08, peak * 1.08)
        axes[2].plot(data["times"], data["f0"], lw=1.0, alpha=0.8, label=name, color=colors[name])
        axes[3].plot(data["times"], data["rmsDb"], lw=1.0, label=name, color=colors[name])
        axes[4].plot(data["times"], data["centroid"], lw=1.0, label=name, color=colors[name])
        axes[5].plot(data["times"], data["voicedProbability"], lw=1.0, label=name, color=colors[name])
    target_x, target_y = [], []
    for note in notes:
        target = 440.0 * 2.0 ** ((note["midi"] - 69) / 12.0)
        target_x += [note["start"], note["end"]]
        target_y += [target, target]
        for axis in axes:
            axis.axvline(note["start"], color="#777777", alpha=0.18, lw=0.7)
        romanized = {"あ": "a", "だ": "da", "ち": "chi", "れ": "re", "い": "i", "う": "u"}
        axes[2].text((note["start"] + note["end"]) / 2, 670, romanized.get(note["lyric"], note["lyric"]), ha="center", va="top", fontsize=10)
    axes[2].plot(target_x, target_y, color="#202020", lw=2, linestyle="--", label="MIDI target")
    labels = [
        ("VocalRack waveform + RMS envelope", "Amplitude"),
        (f"{comparison_label} waveform + RMS envelope", "Amplitude"),
        ("Per-frame pYIN pitch", "Frequency (Hz)"),
        ("Short-time loudness", "RMS (dBFS)"),
        ("Spectral brightness", "Centroid (Hz)"),
        ("Pitch-track confidence / periodic stability", "pYIN probability"),
    ]
    for axis, (title, ylabel) in zip(axes, labels):
        axis.set_title(title, loc="left", fontsize=11, fontweight="bold")
        axis.set_ylabel(ylabel)
        axis.grid(alpha=0.18)
        axis.set_xlim(0, duration)
    axes[2].set_ylim(60, 700)
    axes[3].set_ylim(-75, 0)
    axes[4].set_ylim(0, max(6000, axes[4].get_ylim()[1]))
    axes[5].set_ylim(0, 1.02)
    axes[-1].set_xlabel("Time (seconds)")
    axes[1].set_ylabel("")
    axes[0].legend(loc="upper right")
    axes[1].legend(loc="upper right")
    axes[2].legend(loc="upper right", ncol=3)
    fig.suptitle("Same-USTX acoustic comparison", fontsize=16, fontweight="bold")
    fig.savefig(out / "deep-analysis.png", dpi=160)
    plt.close(fig)


def plot_waveform_detail(out: pathlib.Path, signals: dict, comparison_label: str) -> None:
    names = ["VocalRack", comparison_label]
    colors = {"VocalRack": "#d33f6a", comparison_label: "#2b78d0"}
    windows = [
        (0.0, 3.5, "Whole neutral phrase"),
        (0.43, 0.62, "a to da boundary (consonant detail)"),
        (2.24, 2.29, "Sustained i (50 ms cycle detail)"),
    ]
    fig, axes = plt.subplots(len(windows), 2, figsize=(16, 9), constrained_layout=True)
    for row, (start, end, title) in enumerate(windows):
        local_peak = 0.0
        sliced = {}
        for name in names:
            signal, rate = signals[name]
            lo, hi = max(0, round(start * rate)), min(len(signal), round(end * rate))
            segment = signal[lo:hi]
            sliced[name] = (segment, rate, lo)
            if len(segment):
                local_peak = max(local_peak, float(np.max(np.abs(segment))))
        limit = max(0.05, local_peak * 1.08)
        for col, name in enumerate(names):
            axis = axes[row, col]
            segment, rate, lo = sliced[name]
            stride = max(1, len(segment) // (7000 if row else 4500))
            x = (lo + np.arange(0, len(segment), stride)) / rate
            axis.plot(x, segment[::stride], lw=0.55 if row else 0.4, color=colors[name])
            axis.axvline(0.5, color="#333333", ls="--", lw=0.9, alpha=0.65) if row == 1 else None
            axis.set_xlim(start, end)
            axis.set_ylim(-limit, limit)
            axis.grid(alpha=0.18)
            axis.set_title(f"{name}: {title}", loc="left", fontsize=11, fontweight="bold")
            axis.set_ylabel("Amplitude")
            axis.set_xlabel("Time (seconds)")
    fig.suptitle("A/C waveform comparison: one scale per row", fontsize=16, fontweight="bold")
    fig.savefig(out / "waveform-detail.png", dpi=180)
    plt.close(fig)


def plot_texture_detail(out: pathlib.Path, signals: dict, comparison_label: str) -> None:
    names = ["VocalRack", comparison_label]
    colors = {"VocalRack": "#d33f6a", comparison_label: "#2b78d0"}
    start, end, expected_f0 = 2.10, 2.90, 246.94
    cycle_sets = {}
    spectrograms = {}
    shared_spectrum_peak = 1e-12
    for name in names:
        signal, rate = signals[name]
        segment = signal[round(start * rate):round(end * rate)]
        expected_period = rate / expected_f0
        peaks, _ = find_peaks(segment, distance=max(2, round(expected_period * 0.72)), prominence=0.025)
        cycles = []
        for left, right in zip(peaks[:-1], peaks[1:]):
            length = right - left
            if length < expected_period * 0.72 or length > expected_period * 1.30:
                continue
            source_phase = np.linspace(0.0, 1.0, length, endpoint=False)
            target_phase = np.linspace(0.0, 1.0, 240, endpoint=False)
            cycle = np.interp(target_phase, source_phase, segment[left:right])
            cycle -= np.mean(cycle)
            rms = float(np.sqrt(np.mean(cycle * cycle)))
            if rms > 1e-5:
                cycles.append(cycle / rms)
        cycle_sets[name] = np.asarray(cycles)
        spectrum = np.abs(librosa.stft(segment, n_fft=1024, hop_length=128, window="hann"))
        spectrograms[name] = (spectrum, rate)
        shared_spectrum_peak = max(shared_spectrum_peak, float(np.max(spectrum)))

    fig, axes = plt.subplots(2, 2, figsize=(16, 8.5), constrained_layout=True)
    for col, name in enumerate(names):
        cycles = cycle_sets[name]
        phase = np.linspace(0.0, 1.0, cycles.shape[1] if cycles.size else 240, endpoint=False)
        for cycle in cycles[:160]:
            axes[0, col].plot(phase, cycle, color=colors[name], alpha=0.10, lw=0.55)
        if cycles.size:
            axes[0, col].plot(phase, np.mean(cycles, axis=0), color="#202020", lw=1.8, label="Mean cycle")
        axes[0, col].set_title(f"{name}: phase-aligned i cycles (n={len(cycles)})", loc="left", fontsize=11, fontweight="bold")
        axes[0, col].set_xlabel("Cycle phase")
        axes[0, col].set_ylabel("RMS-normalized amplitude")
        axes[0, col].set_xlim(0, 1)
        axes[0, col].grid(alpha=0.18)
        axes[0, col].legend(loc="upper right")

        spectrum, rate = spectrograms[name]
        db = 20.0 * np.log10(np.maximum(spectrum, 1e-12) / shared_spectrum_peak)
        frequencies = librosa.fft_frequencies(sr=rate, n_fft=1024)
        keep = frequencies <= 6000
        axes[1, col].imshow(
            db[keep], origin="lower", aspect="auto", cmap="magma", vmin=-70, vmax=0,
            extent=[start, end, float(frequencies[keep][0]), float(frequencies[keep][-1])])
        axes[1, col].set_title(f"{name}: sustained i spectrum on one dB scale", loc="left", fontsize=11, fontweight="bold")
        axes[1, col].set_xlabel("Time (seconds)")
        axes[1, col].set_ylabel("Frequency (Hz)")
    fig.suptitle("A/C sustained-vowel texture: cycle consistency and harmonic energy", fontsize=16, fontweight="bold")
    fig.savefig(out / "texture-detail.png", dpi=180)
    plt.close(fig)

    cycle_fig, cycle_axes = plt.subplots(1, 2, figsize=(16, 5.4), sharex=True, sharey=True, constrained_layout=True)
    for col, name in enumerate(names):
        cycles = cycle_sets[name]
        phase = np.linspace(0.0, 1.0, cycles.shape[1] if cycles.size else 240, endpoint=False)
        if cycles.size:
            low, median, high = np.percentile(cycles, [10, 50, 90], axis=0)
            cycle_axes[col].fill_between(phase, low, high, color=colors[name], alpha=0.22, label="10-90% cycle spread")
            cycle_axes[col].plot(phase, median, color="#202020", lw=2.2, label="Median cycle")
        cycle_axes[col].set_title(f"{name}: phase-aligned sustained i", loc="left", fontsize=12, fontweight="bold")
        cycle_axes[col].set_xlabel("Cycle phase")
        cycle_axes[col].set_ylabel("RMS-normalized amplitude")
        cycle_axes[col].set_xlim(0, 1)
        cycle_axes[col].grid(alpha=0.18)
        cycle_axes[col].legend(loc="upper right")
    cycle_fig.suptitle("A/C cycle-shape zoom", fontsize=17, fontweight="bold")
    cycle_fig.savefig(out / "cycle-shape-zoom.png", dpi=220)
    plt.close(cycle_fig)

    spectrum_fig = plt.figure(figsize=(16, 8.5), constrained_layout=True)
    spectrum_grid = spectrum_fig.add_gridspec(2, 2, height_ratios=[1.45, 1.0])
    trace_axis = spectrum_fig.add_subplot(spectrum_grid[1, :])
    for col, name in enumerate(names):
        axis = spectrum_fig.add_subplot(spectrum_grid[0, col])
        spectrum, rate = spectrograms[name]
        frequencies = librosa.fft_frequencies(sr=rate, n_fft=1024)
        keep = (frequencies >= 500) & (frequencies <= 3000)
        db = 20.0 * np.log10(np.maximum(spectrum, 1e-12) / shared_spectrum_peak)
        axis.imshow(
            db[keep], origin="lower", aspect="auto", cmap="magma", vmin=-70, vmax=0,
            extent=[start, end, float(frequencies[keep][0]), float(frequencies[keep][-1])])
        axis.set_title(f"{name}: 0.5-3 kHz detail", loc="left", fontsize=12, fontweight="bold")
        axis.set_xlabel("Time (seconds)")
        axis.set_ylabel("Frequency (Hz)")
        median_spectrum = np.median(db, axis=1)
        trace_axis.plot(frequencies[keep], median_spectrum[keep], color=colors[name], lw=1.5, label=name)
    trace_axis.axvspan(1300, 2100, color="#777777", alpha=0.09, label="Observed mid-band difference")
    trace_axis.set_xlim(500, 3000)
    trace_axis.set_ylim(-70, 0)
    trace_axis.set_xlabel("Frequency (Hz)")
    trace_axis.set_ylabel("Median magnitude (dB, shared reference)")
    trace_axis.set_title("Median sustained-i spectrum", loc="left", fontsize=12, fontweight="bold")
    trace_axis.grid(alpha=0.18)
    trace_axis.legend(loc="upper right", ncol=3)
    spectrum_fig.suptitle("A/C mid-band spectrum zoom", fontsize=17, fontweight="bold")
    spectrum_fig.savefig(out / "spectrum-zoom.png", dpi=220)
    plt.close(spectrum_fig)


def markdown(result: dict, comparison_label: str) -> str:
    lines = [
        "# Same-USTX deep acoustic analysis", "",
        f"| Note | MIDI target | VocalRack median | VocalRack cents | {comparison_label} median | {comparison_label} cents |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    vr, ou = result["VocalRack"]["notes"], result[comparison_label]["notes"]
    for left, right in zip(vr, ou):
        def value(note, group):
            item = note[group]["median"]
            return "n/a" if item is None else f"{item:.1f}"
        lines.append(
            f"| {left['lyric']} | {left['targetHz']:.1f} Hz | {value(left, 'f0Hz')} Hz | "
            f"{value(left, 'centsError')} | {value(right, 'f0Hz')} Hz | {value(right, 'centsError')} |")
    lines += ["", "Spectral flatness approximates noise or buzz. Centroid and high-band ratio measure brightness; warmth ratio divides 100-1200 Hz energy by total energy. See `deep-analysis.json` for per-note dropouts and spectral values.", ""]
    return "\n".join(lines)


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit("usage: analyze_ab_deep.py VOCALRACK.wav OPENUTAU.wav FIXTURE.ustx OUT_DIR OPENUTAU_LABEL")
    vocal_path, open_path, fixture, out = map(pathlib.Path, sys.argv[1:5])
    comparison_label = sys.argv[5]
    out.mkdir(parents=True, exist_ok=True)
    _, notes = notes_from_ustx(fixture)
    signals = {"VocalRack": load_mono(vocal_path), comparison_label: load_mono(open_path)}
    summaries, tracks = {}, {}
    for name, (signal, rate) in signals.items():
        summaries[name], tracks[name] = analyze(signal, rate, notes)
    pitch_failures = []
    for note in summaries["VocalRack"]["notes"]:
        cents = note["centsError"]["median"]
        if cents is None or abs(cents) > 80.0 or note["voicedFraction"] < 0.70:
            pitch_failures.append({
                "lyric": note["lyric"],
                "medianCents": cents,
                "voicedFraction": note["voicedFraction"],
            })
    result = {
        "fixture": str(fixture),
        "method": {
            "pitch": "librosa pYIN, 4096-frame/256-hop, 65-700 Hz",
            "spectral": "4096-bin Hann STFT",
            "noteWindow": "consonant/release insets with relative activity gate",
        },
        "checks": {
            "vocalRackEveryNoteWithin80CentsAnd70PercentVoiced": not pitch_failures,
            "failures": pitch_failures,
        },
        **summaries,
    }
    (out / "deep-analysis.json").write_text(json.dumps(rounded(result), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out / "deep-analysis.md").write_text(markdown(result, comparison_label), encoding="utf-8")
    plot(out, notes, signals, tracks, comparison_label)
    plot_waveform_detail(out, signals, comparison_label)
    plot_texture_detail(out, signals, comparison_label)
    print(markdown(result, comparison_label))
    if pitch_failures:
        raise SystemExit(f"VocalRack pYIN regression failed: {pitch_failures}")


if __name__ == "__main__":
    main()
