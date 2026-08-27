#!/usr/bin/env python3
"""Write a concise human-readable report from the two regression JSON files."""

import argparse
import json
import pathlib


def mark(value: bool) -> str:
    return "PASS" if value else "FAIL"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("corpus", type=pathlib.Path)
    parser.add_argument("expression", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    corpus = json.loads(args.corpus.read_text(encoding="utf-8"))
    expression = json.loads(args.expression.read_text(encoding="utf-8"))
    summary = corpus["summary"]
    pitch = corpus["pitchSummary"]
    texture = corpus["textureSummary"]
    continuation = corpus["continuationSummary"]
    lines = [
        "# VocalRack / OpenUtau Classic Adachi Rei regression", "",
        f"Overall result: **{mark(corpus['passed'] and expression['passed'])}**", "",
        "Both renders use the same generated/fixed USTX fixtures and the bundled official Adachi Rei bank. "
        "The reference is pinned OpenUtau Classic with its built-in `worldline` resampler and `convergence` wavtool. "
        "The goal is perceptual and performance equivalence, not phase-identical samples.", "",
        "## Coverage", "",
        f"- {corpus['aliasAudit']['vocalRackSelectedAliases']} VocalRack notes and "
        f"{corpus['aliasAudit']['openUtauSelectedAliases']} OpenUtau notes rendered.",
        f"- {summary['boundaries']} boundaries measured, including all available VCV aliases and all ordered articulation-class joins.",
        f"- {pitch['notesMeasured']} of {pitch['eligibleNotes']} eligible sustained notes measured for pitch "
        f"({pitch['measurementCoverage'] * 100:.2f}% coverage); {pitch['excludedTransientNotes']} sub-180 ms "
        "consonant-dominant notes remain covered by boundary/duration checks.",
        f"- {texture['notesMeasured']} notes measured for timbre ({texture['measurementCoverage'] * 100:.2f}% coverage).",
        "- Separate flat pitch, two authored vibratos (including phase shift), an authored pitch curve, and a sparse dynamics curve.",
        "- All bundled OTO entries are validated by the native official-bank test; this corpus exercises the normal Japanese CV/VCV linguistic inventory.",
        "", "## Corpus results", "", "| Measure | Result |", "|---|---:|",
        f"| Median zero-lag boundary correlation | {summary['zeroLagCorrelation']['median']:.4f} |",
        f"| Median best-lag boundary correlation | {summary['bestLagCorrelation']['median']:.4f} |",
        f"| P10 best-lag boundary correlation | {summary['bestLagCorrelation']['p10']:.4f} |",
        f"| Median / p90 absolute boundary lag | {summary['absoluteBestLagMs']['median']:.1f} / {summary['absoluteBestLagMs']['p90']:.1f} ms |",
        f"| Median / p90 A:C pitch difference | {pitch['medianAbsoluteDifferenceCents']:.2f} / {pitch['p90AbsoluteDifferenceCents']:.2f} cents |",
        f"| Worst reliable A:C pitch difference | {pitch['maximumAbsoluteReliableDifferenceCents']:.2f} cents |",
        f"| Worst VocalRack target-pitch error | {pitch['vocalRackMaximumAbsoluteTargetErrorCents']:.2f} cents |",
        f"| Median / p10 log-mel correlation | {texture['melEnvelopeCorrelation']['median']:.4f} / {texture['melEnvelopeCorrelation']['p10']:.4f} |",
        f"| Median / p90 log-mel RMSE | {texture['melEnvelopeRmseDb']['median']:.2f} / {texture['melEnvelopeRmseDb']['p90']:.2f} dB |",
        f"| P90 absolute level difference | {texture['absoluteRmsDifferenceDb']['p90']:.2f} dB |",
        f"| P90 warmth / high-band ratio difference | {texture['absoluteWarmthRatioDifference']['p90']:.4f} / {texture['absoluteHighBandRatioDifference']['p90']:.4f} |",
        f"| Continuations: median best-lag correlation / lag | {continuation['medianBestLagCorrelation']:.4f} / {continuation['medianAbsoluteBestLagMs']:.1f} ms |",
        f"| Continuations: median pitch difference / log-mel correlation | {continuation['medianAbsolutePitchDifferenceCents']:.2f} cents / {continuation['medianMelEnvelopeCorrelation']:.4f} |",
        "", "## Expression results", "",
        "| Case | Best correlation | Aligned RMSE | Lag |", "|---|---:|---:|---:|",
    ]
    for row in expression["pitchSegments"]:
        if row["kind"] == "flat":
            lines.append(
                f"| {row['name']} | target error A {abs(row['vocalRackMedianCents']):.2f} cents | "
                f"A:C median {abs(row['vocalRackMedianCents'] - row['openUtauClassicMedianCents']):.2f} cents | n/a |")
        else:
            lines.append(
                f"| {row['name']} | {row['bestLagCorrelation']:.4f} | {row['alignedRmseCents']:.2f} cents | "
                f"{row['bestLagMsOpenUtauRelativeToVocalRack']:+d} ms |")
    for row in expression["dynamicsSegments"]:
        lines.append(
            f"| {row['name']} | {row['bestLagCorrelation']:.4f} | {row['alignedRmseDb']:.3f} dB | "
            f"{row['bestLagMsOpenUtauRelativeToVocalRack']:+d} ms |")
    failed = [name for name, passed in {**corpus["checks"], **expression["checks"]}.items() if not passed]
    lines.extend([
        "", "## Reproduce", "", "```sh", "make openutau-regression", "```", "",
        "OpenUtau, .NET/NuGet, librosa, NumPy, SciPy, and plotting run in disposable Docker containers. "
        "Inspect `regression-report.json`, `expression-report.json`, the separate shared-scale PNG plots, and "
        "`regression-worst-ac-sequential.wav` for machine-readable and listening evidence.", "",
        f"Failed checks: {', '.join(failed) if failed else 'none'}.", "",
    ])
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
