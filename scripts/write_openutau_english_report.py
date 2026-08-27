#!/usr/bin/env python3
"""Write the combined Adachi/X-SAMPA/VCCV English comparison report."""

import argparse
import json
import pathlib


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("adachi", type=pathlib.Path)
    parser.add_argument("xsampa", type=pathlib.Path)
    parser.add_argument("vccv", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    reports = [json.loads(path.read_text(encoding="utf-8"))
               for path in (args.adachi, args.xsampa, args.vccv)]
    passed = all(report["passed"] for report in reports)
    lines = [
        "# VocalRack / pinned OpenUtau English regression",
        "",
        f"Overall result: **{'PASS' if passed else 'FAIL'}**",
        "",
        "Each row renders the same generated USTX and the same singer in VocalRack and pinned "
        "OpenUtau Classic. Adachi Rei uses English-to-Japanese phonemization; the X-SAMPA and "
        "VCCV rows use deterministic, copyright-safe synthetic UTAU banks.",
        "",
        "| Suite | Notes | Phones matched/reference | Recall / precision | Phone timing p90 | Boundaries | Best-lag r (median) | Lag p90 | Pitch p90 | Mel r (median) | Result |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for report in reports:
        boundary = report["summary"]
        pitch = report["pitchSummary"]
        texture = report["textureSummary"]
        phones = report["phoneSequenceSummary"]
        lines.append(
            f"| {report['label']} | {pitch['eligibleNotes']} | "
            f"{phones['orderedAliasMatches']}/{phones['openUtauPhones']} | "
            f"{phones['openUtauRecall'] * 100:.1f}% / {phones['vocalRackPrecision'] * 100:.1f}% | "
            f"{phones['alignedAbsoluteTimingMs']['p90']:.1f} ms | "
            f"{boundary['boundaries']} | "
            f"{boundary['bestLagCorrelation']['median']:.3f} | "
            f"{boundary['absoluteBestLagMs']['p90']:.1f} ms | "
            f"{pitch['p90AbsoluteDifferenceCents']:.1f} cents | "
            f"{texture['melEnvelopeCorrelation']['median']:.3f} | "
            f"{'PASS' if report['passed'] else 'FAIL'} |"
        )
    failed = [f"{report['label']}: {name}"
              for report in reports for name, ok in report["checks"].items() if not ok]
    lines.extend([
        "",
        "## Evidence",
        "",
        "Each suite checks that both render logs contain the canonical phoneme aliases in the same "
        "order. It also checks aligned phone timing, boundary and transient behavior, sustained "
        "pYIN/YIN pitch, RMS level, log-mel envelope, spectral centroid and flatness, and low- and "
        "high-band ratios. The report includes per-group plots, the twelve worst boundaries, and a "
        "sequential A/C listening WAV for the eight worst cases. The Adachi corpus contains 119 notes "
        "in 25 phrases. It covers ordinary dictionary words, major consonant classes, clusters, "
        "diphthongs, multisyllabic lyrics, connected phrases, continuations, registers, and explicit "
        "internal consonant boundaries.",
        "",
        "Reproduce with:",
        "",
        "```sh",
        "make openutau-english-regression",
        "```",
        "",
        f"Failed checks: {', '.join(failed) if failed else 'none'}.",
        "",
    ])
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
