#!/usr/bin/env python3
"""Fast boundary-only scorer for iterating on the Classic regression."""

import argparse
import json
import pathlib

import numpy as np

from analyze_openutau_regression import correlation, envelope, load_audio, percentile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("vocalrack", type=pathlib.Path)
    parser.add_argument("classic", type=pathlib.Path)
    parser.add_argument("manifest", type=pathlib.Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    vocal, vocal_rate = load_audio(args.vocalrack)
    classic, classic_rate = load_audio(args.classic)
    if vocal_rate != classic_rate:
        raise ValueError("sample-rate mismatch")
    vocal /= max(float(np.max(np.abs(vocal))), 1e-12)
    classic /= max(float(np.max(np.abs(classic))), 1e-12)
    vocal_times, vocal_env = envelope(vocal, vocal_rate)
    classic_times, classic_env = envelope(classic, classic_rate)
    rows = []
    for boundary_index, item in enumerate(manifest["boundaries"], start=1):
        boundary = float(item["seconds"])
        grid = np.arange(boundary - 0.120, boundary + 0.160, 0.001)
        left = np.interp(grid, vocal_times, vocal_env, left=0, right=0)
        right = np.interp(grid, classic_times, classic_env, left=0, right=0)
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
        rows.append({
            **item,
            "boundaryIndex": boundary_index,
            "zeroLagCorrelation": correlation(left, right),
            "bestLagCorrelation": best_corr,
            "absoluteBestLagMs": abs(best_lag),
        })
    groups = {}
    for group in sorted({row["group"].split(":", 1)[0] for row in rows}):
        selected = [row for row in rows if row["group"].split(":", 1)[0] == group]
        groups[group] = {
            "boundaries": len(selected),
            "medianZeroLagCorrelation": float(np.median([
                row["zeroLagCorrelation"] for row in selected])),
            "p10BestLagCorrelation": percentile([
                row["bestLagCorrelation"] for row in selected], 10),
            "p90AbsoluteBestLagMs": percentile([
                row["absoluteBestLagMs"] for row in selected], 90),
        }
    result = {
        "boundaries": len(rows),
        "medianZeroLagCorrelation": float(np.median([
            row["zeroLagCorrelation"] for row in rows])),
        "p10BestLagCorrelation": percentile([
            row["bestLagCorrelation"] for row in rows], 10),
        "medianAbsoluteBestLagMs": float(np.median([
            row["absoluteBestLagMs"] for row in rows])),
        "p90AbsoluteBestLagMs": percentile([
            row["absoluteBestLagMs"] for row in rows], 90),
        "groups": groups,
        "worst": sorted(rows, key=lambda row: row["bestLagCorrelation"])[:20],
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
