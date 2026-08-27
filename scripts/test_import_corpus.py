#!/usr/bin/env python3
"""Probe real-world USTX, UST, and MIDI files through the production importer."""

import argparse
import json
import pathlib
import subprocess
import sys


EXTENSIONS = {".ustx", ".ust", ".mid", ".midi"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=pathlib.Path)
    parser.add_argument("--renderer", type=pathlib.Path, default=pathlib.Path("build-tests/vocalrack-render"))
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    files: list[pathlib.Path] = []
    for source in args.paths:
        if source.is_dir():
            files.extend(path for path in source.rglob("*") if path.suffix.lower() in EXTENSIONS)
        elif source.suffix.lower() in EXTENSIONS:
            files.append(source)
    files = sorted(set(path.resolve() for path in files))
    if not files:
        raise SystemExit("No .ustx, .ust, .mid, or .midi files found")
    rows = []
    for path in files:
        completed = subprocess.run(
            [str(args.renderer), "--inspect", "--project", str(path)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        rows.append({"file": path.name, "passed": completed.returncode == 0, "output": completed.stdout.strip()})
        print(("PASS" if completed.returncode == 0 else "FAIL"), path)
        if completed.stdout:
            print(completed.stdout.rstrip())
    result = {"schemaVersion": 1, "files": len(rows), "passed": all(row["passed"] for row in rows), "results": rows}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    raise SystemExit(0 if result["passed"] else 1)


if __name__ == "__main__":
    main()
