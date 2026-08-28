#!/usr/bin/env python3
"""Generate English OpenUtau comparison corpora and synthetic test banks."""

import argparse
import json
import math
import pathlib
import struct
import wave


BPM = 120
TPQ = 480
SAMPLE_RATE = 44100


def seconds(tick: int) -> float:
    return tick * 60.0 / (BPM * TPQ)


def write_test_bank(root: pathlib.Path, singer_id: str, aliases: list[str]) -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "character.txt").write_text(
        f"name=VocalRack {singer_id} regression bank\nauthor=VocalRack test fixture\n",
        encoding="utf-8",
    )
    frames = bytearray()
    for index in range(SAMPLE_RATE):
        time = index / SAMPLE_RATE
        fade = min(1.0, time / 0.02, (1.0 - time) / 0.04)
        sample = 0.28 * max(0.0, fade) * (
            math.sin(2.0 * math.pi * 220.0 * time)
            + 0.16 * math.sin(2.0 * math.pi * 440.0 * time)
        )
        frames.extend(struct.pack("<h", round(max(-1.0, min(1.0, sample)) * 32767)))
    with wave.open(str(root / "voice.wav"), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(frames)
    (root / "oto.ini").write_text(
        "".join(f"voice.wav={alias},0,80,-900,45,15\n" for alias in aliases),
        encoding="utf-8",
    )


def reference_aliases(root: pathlib.Path) -> list[str]:
    """Reuse only the pinned test bank's public alias inventory, not its audio."""
    aliases = set()
    for oto in root.rglob("oto.ini"):
        for line in oto.read_text(encoding="utf-8-sig", errors="replace").splitlines():
            if "=" not in line:
                continue
            values = line.split("=", 1)[1].split(",", 1)
            if values[0].strip():
                aliases.add(values[0].strip())
    return sorted(aliases)


def build_notes(phrases: list[tuple[str, list[str], list[int] | None]]) -> tuple[list[dict], list[dict], int]:
    notes: list[dict] = []
    boundaries: list[dict] = []
    tick = 0
    contour = [55, 57, 59, 60, 59, 57, 55]
    for phrase_index, (group, lyrics, tones) in enumerate(phrases, start=1):
        tones = tones or [contour[index % len(contour)] for index in range(len(lyrics))]
        local: list[dict] = []
        for index, lyric in enumerate(lyrics):
            duration = 960 if not lyric.startswith("+") else 480
            note = {
                "position": tick,
                "duration": duration,
                "tone": tones[index],
                "lyric": lyric,
                "group": group,
                "phraseId": phrase_index,
            }
            notes.append(note)
            if local:
                prior = local[-1]
                boundaries.append({
                    "tick": tick,
                    "seconds": seconds(tick),
                    "group": group,
                    "label": f"{prior['lyric']} to {lyric}",
                    "leftLyric": prior["lyric"],
                    "rightLyric": lyric,
                    "expectedAlias": None,
                    "leftTone": prior["tone"],
                    "rightTone": note["tone"],
                    "leftDurationTick": prior["duration"],
                    "rightDurationTick": duration,
                })
            local.append(note)
            tick += duration
        tick += 480
    return notes, boundaries, tick


def write_suite(root: pathlib.Path, singer: str, phonemizer: str,
                phrases: list[tuple[str, list[str], list[int] | None]], title: str,
                expected_aliases: list[str] | None = None,
                internal_boundaries: list[dict] | None = None) -> None:
    notes, boundaries, duration = build_notes(phrases)
    for spec in internal_boundaries or []:
        note = next((item for item in notes
                     if item["group"] == spec["group"] and item["lyric"] == spec["lyric"]), None)
        if note is None:
            raise ValueError(f"Internal-boundary note not found: {spec}")
        tick = note["position"] + spec["offsetTick"]
        boundaries.append({
            "tick": tick,
            "seconds": seconds(tick),
            "group": "internal:" + spec["group"],
            "label": spec["label"],
            "leftLyric": spec["left"],
            "rightLyric": spec["right"],
            "expectedAlias": spec["right"],
            "leftTone": note["tone"],
            "rightTone": note["tone"],
            "leftDurationTick": spec.get("leftDurationTick", 55),
            "rightDurationTick": spec.get("rightDurationTick", 55),
            "internalPhoneBoundary": True,
        })
    boundaries.sort(key=lambda item: (item["tick"], item["group"], item["label"]))
    root.mkdir(parents=True, exist_ok=True)
    lines = [
        f"name: {json.dumps(title)}",
        "comment: Generated English VocalRack/OpenUtau comparison fixture.",
        "ustx_version: 0.9",
        "resolution: 480",
        "tempos:",
        f"- {{position: 0, bpm: {BPM}}}",
        "time_signatures:",
        "- {bar_position: 0, beat_per_bar: 4, beat_unit: 4}",
        "tracks:",
        f"- track_name: {json.dumps(title)}",
        f"  singer: {json.dumps(singer)}",
        f"  phonemizer: {json.dumps(phonemizer)}",
        "  renderer_settings:",
        "    renderer: CLASSIC",
        "voice_parts:",
        f"- name: {json.dumps(title)}",
        "  track_no: 0",
        "  position: 0",
        f"  duration: {duration}",
        "  notes:",
    ]
    for note in notes:
        lines.extend([
            f"  - position: {note['position']}",
            f"    duration: {note['duration']}",
            f"    tone: {note['tone']}",
            f"    lyric: {json.dumps(note['lyric'])}",
            "    pitch:",
            "      snap_first: true",
            "      data:",
            "      - {x: -40, y: 0, shape: io}",
            "      - {x: 40, y: 0, shape: io}",
            "    vibrato: {length: 0, period: 185, depth: 0, in: 10, out: 10, shift: 0}",
        ])
    (root / "corpus.ustx").write_text("\n".join(lines) + "\n", encoding="utf-8")
    manifest = {
        "schemaVersion": 1,
        "bpm": BPM,
        "ticksPerQuarter": TPQ,
        "notes": len(notes),
        "expectedAliases": expected_aliases or [],
        "noteEvents": [{
            "seconds": seconds(note["position"]),
            "durationSeconds": seconds(note["duration"]),
            "tone": note["tone"],
            "lyric": note["lyric"],
            "group": note["group"],
        } for note in notes],
        "boundaries": boundaries,
        "durationSeconds": seconds(duration),
        "coverage": {
            "phrases": len(phrases),
            "wordsOrExtenders": len(notes),
            "continuationBoundaries": sum(
                row["group"].split(":", 1)[0] == "continuation" for row in boundaries),
            "internalPhoneBoundaries": sum(
                bool(row.get("internalPhoneBoundary")) for row in boundaries),
        },
    }
    (root / "corpus-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def write_exact_template_manifest(root: pathlib.Path, score_path: pathlib.Path) -> None:
    """Describe the shipped English first-sound score for acoustic analysis.

    The USTX itself is produced by the production C++ exporter in the runner,
    so this row exercises exactly the score, interchange path, phonemizer and
    renderer users receive rather than a separately hand-authored lookalike.
    """
    score = json.loads(score_path.read_text(encoding="utf-8"))
    notes = score["notes"]
    group = "template:wake-up-little-machine"
    boundaries = []
    for left, right in zip(notes, notes[1:]):
        tick = right["startTick"]
        boundaries.append({
            "tick": tick,
            "seconds": seconds(tick),
            "group": group,
            "label": f"{left['lyric']} to {right['lyric']}",
            "leftLyric": left["lyric"],
            "rightLyric": right["lyric"],
            "expectedAlias": None,
            "leftTone": left["midiNote"],
            "rightTone": right["midiNote"],
            "leftDurationTick": left["durationTick"],
            "rightDurationTick": right["durationTick"],
        })
    boundaries.sort(key=lambda item: (item["tick"], item["group"], item["label"]))
    duration = max(note["startTick"] + note["durationTick"] for note in notes)
    manifest = {
        "schemaVersion": 1,
        "bpm": score["nominalBpm"],
        "ticksPerQuarter": TPQ,
        "notes": len(notes),
        "expectedAliases": [],
        "noteEvents": [{
            "seconds": seconds(note["startTick"]),
            "durationSeconds": seconds(note["durationTick"]),
            "tone": note["midiNote"],
            "lyric": note["lyric"],
            "group": group,
        } for note in notes],
        "boundaries": boundaries,
        "durationSeconds": seconds(duration),
        "coverage": {
            "phrases": 1,
            "wordsOrExtenders": len(notes),
            "continuationBoundaries": sum(note["lyric"].startswith("+") for note in notes),
            "internalPhoneBoundaries": 0,
        },
    }
    root.mkdir(parents=True, exist_ok=True)
    (root / "corpus-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=pathlib.Path)
    args = parser.parse_args()
    output = args.output_dir
    repo = pathlib.Path(__file__).resolve().parent.parent
    write_exact_template_manifest(
        output / "template", repo / "tests" / "fixtures" / "english_first_sound.json")

    broad_adachi_phrases = [
        ("matrix:function", ["and", "are", "be", "can", "do"], None),
        ("matrix:pronouns", ["i", "me", "my", "you", "we"], None),
        ("matrix:short", ["in", "is", "it", "of", "on"], None),
        ("matrix:phonetic", ["barn", "cute", "itch", "its", "read", "rack"], None),
        ("matrix:product", ["hello", "openutau", "utau", "vocal", "vocalrack", "voice"], None),
        ("matrix:connectors", ["the", "this", "to", "with", "for"], None),
        ("matrix:vowels", ["all", "no", "soon", "tea", "vu"], None),
        # A dictionary-scale acoustic sample: every major English consonant
        # class, diphthongs, onset/coda clusters, and longer words. These are
        # ordinary CMUdict lyrics, not phonetic hints tailored to our engine.
        ("matrix:stops", ["pack", "bad", "tap", "dad", "kick", "gig", "cake", "dog"], None),
        ("matrix:fricatives", ["fan", "van", "thin", "then", "sip", "zip", "fish", "vision"], None),
        ("matrix:affricate-nasal", ["chip", "judge", "match", "sing", "ring", "moon", "name"], None),
        ("matrix:liquid-glide", ["light", "red", "yes", "water", "world"], None),
        ("matrix:diphthongs", ["day", "night", "boy", "now", "go", "loud", "voice"], None),
        ("matrix:clusters", ["blue", "green", "play", "train", "street", "spring", "sky", "glow"], None),
        ("matrix:multisyllable", ["melody", "harmony", "music", "robot", "computer",
                                  "beautiful", "forever", "together", "synthesizer"], None),
        # Natural connected phrases exercise cross-word consonant transfer,
        # including vowel-initial words that exposed the former "an up" bug.
        ("connected:turn-it-on", ["turn", "it", "on"], None),
        ("connected:sing-again", ["sing", "it", "again"], None),
        ("connected:stars-bright", ["stars", "are", "bright"], None),
        ("connected:take-breath", ["take", "a", "breath"], None),
        ("connected:put-down", ["put", "it", "down"], None),
    ]
    write_suite(
        output / "adachi",
        "adachi-rei",
        "OpenUtau.Plugin.Builtin.ENtoJAPhonemizer",
        [
            ("lexical:canonical", ["test", "words"], None),
            ("lexical:default", ["we", "sing"], None),
            ("cluster:star", ["a", "star"], None),
            ("coda:short", ["an", "up"], None),
            ("continuation:testing", ["testing", "+*", "+", "+"], [55, 57, 59, 60]),
            ("pitch:register", ["test", "test", "test"], [43, 55, 67]),
        ] + broad_adachi_phrases,
        "Adachi Rei English to Japanese regression",
        ["て", "す", "と", "うぉ", "ど", "ず", "うぃ", "せ", "e ん", "あ", "た", "a う", "ぷ"],
        [
            {"group": "lexical:canonical", "lyric": "test", "offsetTick": 850,
             "label": "test: て to す", "left": "て", "right": "す"},
            {"group": "lexical:canonical", "lyric": "test", "offsetTick": 905,
             "label": "test: す to と", "left": "す", "right": "と"},
            {"group": "lexical:canonical", "lyric": "words", "offsetTick": 790,
             "label": "words: うぉ to ど", "left": "うぉ", "right": "ど"},
            {"group": "lexical:canonical", "lyric": "words", "offsetTick": 845,
             "label": "words: ど to ず", "left": "ど", "right": "ず"},
            {"group": "cluster:star", "lyric": "star", "offsetTick": 0,
             "label": "star: す to た", "left": "す", "right": "た"},
            {"group": "cluster:star", "lyric": "star", "offsetTick": 845,
             "label": "star: た to う", "left": "た", "right": "a う",
             "leftDurationTick": 845, "rightDurationTick": 115},
        ],
    )

    xsampa_reference = repo / "reference" / "OpenUtau" / "OpenUtau.Test" / "Files" / "en_delta0"
    xsampa_aliases = reference_aliases(xsampa_reference) + [
        "- maI", "aI t", "tE", "E st-", "- ri", "i d-",
        "- haI", "aI -", "aI h", "haI", "- vOI", "OI s-", "- V", "V -",
    ]
    write_test_bank(output / "xsampa" / "voicebank", "EN X-SAMPA", xsampa_aliases)
    (output / "xsampa" / "voicebank" / "en-xsampa.yaml").write_text(
        (xsampa_reference / "en-xsampa.yaml").read_text(encoding="utf-8-sig"), encoding="utf-8")
    write_suite(
        output / "xsampa",
        "vocalrack-en-xsampa",
        "OpenUtau.Plugin.Builtin.EnXSampaPhonemizer",
        [
            ("canonical:delta", ["my", "test"], None),
            ("hint:xsampa", ["read [r i d]"], None),
            ("diphthong:hi", ["hi"], None),
            ("coda:voice", ["voice"], None),
            ("continuation:vowel", ["a", "+", "+"], [55, 59, 62]),
            ("pitch:register", ["hi", "hi", "hi"], [43, 55, 67]),
        ],
        "English X-SAMPA regression",
        ["- maI", "aI t", "tE", "E st-", "- ri", "i d-",
         "- haI", "aI -", "- vOI", "OI s-", "- V", "V -"],
    )

    vccv_reference = repo / "reference" / "OpenUtau" / "OpenUtau.Test" / "Files" / "en_vccv"
    vccv_aliases = reference_aliases(vccv_reference) + [
        "-te", "es-", "st", "w3", "3d-", "dz-", "-rE", "Ed-", "-hI", "I-", "I h", "hI",
    ]
    write_test_bank(output / "vccv" / "voicebank", "English VCCV", vccv_aliases)
    write_suite(
        output / "vccv",
        "vocalrack-en-vccv",
        "OpenUtau.Plugin.Builtin.EnglishVCCVPhonemizer",
        [
            ("canonical:vccv", ["test", "words"], None),
            ("hint:vccv", ["read [r E d]"], None),
            ("continuation:vowel", ["hi", "+", "+"], [55, 59, 62]),
            ("pitch:register", ["hi", "hi", "hi"], [43, 55, 67]),
        ],
        "English VCCV regression",
        ["-te", "es-", "st", "w3", "3d-", "dz-", "-rE", "Ed-",
         "-hI", "I-", "I h", "hI"],
    )

    print(json.dumps({name: json.loads((output / name / "corpus-manifest.json").read_text())["notes"]
                      for name in ("adachi", "xsampa", "vccv")}, indent=2))


if __name__ == "__main__":
    main()
