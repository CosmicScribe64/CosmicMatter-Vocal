#!/usr/bin/env python3
"""Validate and finalize the mandatory real-Rack evidence report."""

import json
import math
import pathlib
import struct
import sys
import wave


def wav_values(path: pathlib.Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as wav:
        frames = wav.readframes(wav.getnframes())
        rate = wav.getframerate()
        width = wav.getsampwidth()
        channels = wav.getnchannels()
    if width != 2 or channels != 1:
        raise AssertionError(f"{path.name}: expected mono PCM16")
    values = [int.from_bytes(frames[i:i + 2], "little", signed=True) / 32768.0 for i in range(0, len(frames), 2)]
    if not values or not all(math.isfinite(v) for v in values):
        raise AssertionError(f"{path.name}: empty/non-finite")
    return rate, values


def wav_metrics(path: pathlib.Path) -> dict:
    rate, values = wav_values(path)
    peak = max(abs(v) for v in values)
    rms = math.sqrt(sum(v * v for v in values) / len(values))
    return {"frames": len(values), "sampleRate": rate, "seconds": len(values) / rate, "peak": peak, "rms": rms}


def segment_rms(path: pathlib.Path, start: float, end: float) -> float:
    rate, values = wav_values(path)
    segment = values[max(0, round(start * rate)):min(len(values), round(end * rate))]
    return math.sqrt(sum(value * value for value in segment) / len(segment)) if segment else 0.0


def normalized_difference(left_path: pathlib.Path, right_path: pathlib.Path, seconds: float | None = None) -> float:
    left_rate, left = wav_values(left_path)
    right_rate, right = wav_values(right_path)
    if left_rate != right_rate:
        raise AssertionError("comparison WAV sample-rate mismatch")
    count = min(len(left), len(right), round(seconds * left_rate) if seconds else min(len(left), len(right)))
    reference = math.sqrt(sum(value * value for value in left[:count]) / count)
    difference = math.sqrt(sum((left[i] - right[i]) ** 2 for i in range(count)) / count)
    return difference / max(reference, 1e-9)


def segment_difference(path: pathlib.Path, first_start: float, second_start: float, duration: float) -> float:
    rate, values = wav_values(path)
    count = round(duration * rate)
    first = values[round(first_start * rate):round(first_start * rate) + count]
    second = values[round(second_start * rate):round(second_start * rate) + count]
    count = min(len(first), len(second))
    reference = math.sqrt(sum(value * value for value in first[:count]) / count)
    difference = math.sqrt(sum((first[i] - second[i]) ** 2 for i in range(count)) / count)
    return difference / max(reference, 1e-9)


def longest_quiet_before(path: pathlib.Path, frame_limit: int, threshold: float = 1e-4) -> float:
    with wave.open(str(path), "rb") as wav:
        rate = wav.getframerate()
        frames = wav.readframes(min(frame_limit, wav.getnframes()))
    longest = current = 0
    for i in range(0, len(frames), 2):
        value = int.from_bytes(frames[i:i + 2], "little", signed=True) / 32768.0
        if abs(value) < threshold:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest / rate


def screenshot_metrics(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    if len(data) < 100_000 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("screenshot.png: expected a non-trivial PNG image")
    width, height = struct.unpack(">II", data[16:24])
    if width < 800 or height < 500:
        raise AssertionError(f"screenshot.png: image is too small ({width}x{height})")
    return {"bytes": len(data), "width": width, "height": height}


def main() -> None:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "test-artifacts/e2e")
    for required_evidence in ("rack.log", "screenshot.png", "screenshot-provenance.json", "TEST_REPORT.md"):
        if not (root / required_evidence).is_file():
            raise AssertionError(f"missing required E2E evidence: {required_evidence}")
    screenshot_provenance = json.loads((root / "screenshot-provenance.json").read_text())
    if screenshot_provenance.get("capturedApplication") not in {"Rack", "VCV Rack 2 Free"}:
        raise AssertionError("screenshot provenance does not identify VCV Rack as the captured application")
    raw = json.loads((root / "results.raw.json").read_text())
    reload_result = json.loads((root / "reload-results.json").read_text())
    screenshot = screenshot_metrics(root / "screenshot.png")
    required = [
        "default-first-sound.wav", "clock-120.wav", "clock-60.wav", "pause-resume-reset.wav",
        "one-shot.wav", "loop.wav", "sections.wav", "multiple-instances.wav", "triggered-word.wav",
        "sustained-vowel-baseline.wav", "sustained-vowel-modulated.wav", "sustained-vowel-shaped.wav",
        "sustained-vowel-reverb.wav", "full-song-dry.wav", "full-song-reverb.wav",
    ]
    wave_metrics = {name: wav_metrics(root / name) for name in required}
    scenarios = raw["scenarios"]
    end120, end60 = scenarios["clock120"]["endPulses"], scenarios["clock60"]["endPulses"]
    checks = {
        "A_default_first_sound": wave_metrics["default-first-sound.wav"]["rms"] > 0.005 and bool(scenarios["defaultFirstSound"]["endPulses"]),
        "B_clock_speed": bool(end120) and bool(end60) and 1.85 <= end60[0] / end120[0] <= 2.15
            and longest_quiet_before(root / "clock-120.wav", end120[0]) < 0.1
            and longest_quiet_before(root / "clock-60.wav", end60[0]) < 0.1,
        "C_pause_resume_reset": wave_metrics["pause-resume-reset.wav"]["rms"] > 0.003,
        "D_one_shot": len(scenarios["oneShot"]["endPulses"]) == 1,
        "D_loop": len(scenarios["loop"]["endPulses"]) >= 3,
        "E_sections": len(scenarios["sections"]["endPulses"]) >= 5
            and raw["sectionControl"]["requestedIndices"] == [1, 0, 1]
            and len(raw["sectionControl"]["triggerSeconds"]) == 2
            and segment_difference(root / "sections.wav", 0.25, 3.25, 0.55) > 0.25,
        "F_multiple_instances": wave_metrics["multiple-instances.wav"]["rms"] > 0.005 and scenarios["multipleInstances"]["secondaryEnergy"] > 100.0,
        "G_save_reload": reload_result["rms"] > 0.005 and reload_result["endPulses"] == 1
            and raw["moduleStates"] == reload_result["moduleStates"],
        "H_singer_plate": screenshot["width"] >= 800 and screenshot["height"] >= 500,
        "I_triggered_word": wave_metrics["triggered-word.wav"]["rms"] > 0.004
            and len(scenarios["triggeredWord"]["endPulses"]) == 1,
        "J_sustained_vowel_modulation": wave_metrics["sustained-vowel-baseline.wav"]["rms"] > 0.004
            and normalized_difference(root / "sustained-vowel-baseline.wav", root / "sustained-vowel-modulated.wav") > 0.15,
        "J_external_adsr_vca": segment_rms(root / "sustained-vowel-shaped.wav", 0.5, 2.0) > 0.003
            and segment_rms(root / "sustained-vowel-shaped.wav", 3.1, 3.4)
                < segment_rms(root / "sustained-vowel-shaped.wav", 0.5, 2.0) * 0.12,
        "J_vowel_reverb_tail": segment_rms(root / "sustained-vowel-reverb.wav", 5.7, 7.0) > 0.0001
            and segment_rms(root / "sustained-vowel-reverb.wav", 5.7, 7.0)
                > segment_rms(root / "sustained-vowel-shaped.wav", 5.7, 7.0) * 2.0,
        "K_complete_song": wave_metrics["full-song-dry.wav"]["seconds"] >= 18.9
            and wave_metrics["full-song-dry.wav"]["rms"] > 0.004
            and len(scenarios["fullSongDry"]["endPulses"]) == 1
            and longest_quiet_before(root / "full-song-dry.wav", scenarios["fullSongDry"]["endPulses"][0]) < 0.12,
        "K_song_reverb": normalized_difference(root / "full-song-dry.wav", root / "full-song-reverb.wav", 15.5) > 0.15
            and segment_rms(root / "full-song-reverb.wav", 16.2, 18.5) > 0.0001
            and segment_rms(root / "full-song-dry.wav", 16.2, 18.5) < 0.0001,
        "zero_underruns_after_preroll": all(value == 0 for value in raw["moduleUnderruns"]) and reload_result["underruns"] == 0,
    }
    failures = [name for name, passed in checks.items() if not passed]
    final = {"passed": not failures, "checks": checks, "failures": failures, "raw": raw, "reload": reload_result,
             "screenshot": {**screenshot, "provenance": screenshot_provenance}, "wavMetrics": wave_metrics}
    (root / "results.json").write_text(json.dumps(final, indent=2) + "\n")
    if failures:
        raise SystemExit("E2E validation failed: " + ", ".join(failures))


if __name__ == "__main__":
    main()
