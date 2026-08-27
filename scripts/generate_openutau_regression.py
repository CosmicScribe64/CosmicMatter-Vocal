#!/usr/bin/env python3
"""Generate a deterministic Adachi Rei coverage corpus and boundary manifest."""

import argparse
import json
import pathlib


BPM = 120
TPQ = 480

GOJUON = [
    ("vowels", list("あいうえおん")),
    ("k", list("かきくけこ")),
    ("g", list("がぎぐげご")),
    ("s", list("さしすせそ")),
    ("z", list("ざじずぜぞ")),
    ("t", list("たちつてと")),
    ("d", list("だぢづでど")),
    ("n", list("なにぬねの")),
    ("h", list("はひふへほ")),
    ("b", list("ばびぶべぼ")),
    ("p", list("ぱぴぷぺぽ")),
    ("m", list("まみむめも")),
    ("y", ["や", "ゆ", "よ"]),
    ("r", list("らりるれろ")),
    ("w", ["わ", "を", "ん"]),
]

CONTRACTED = [
    ("ky", ["きゃ", "きゅ", "きょ"]),
    ("gy", ["ぎゃ", "ぎゅ", "ぎょ"]),
    ("sh", ["しゃ", "しゅ", "しょ"]),
    ("j", ["じゃ", "じゅ", "じょ"]),
    ("ch", ["ちゃ", "ちゅ", "ちょ"]),
    ("ny", ["にゃ", "にゅ", "にょ"]),
    ("hy", ["ひゃ", "ひゅ", "ひょ"]),
    ("by", ["びゃ", "びゅ", "びょ"]),
    ("py", ["ぴゃ", "ぴゅ", "ぴょ"]),
    ("my", ["みゃ", "みゅ", "みょ"]),
    ("ry", ["りゃ", "りゅ", "りょ"]),
    ("foreign", ["ふぁ", "ふぃ", "ふぇ", "ふぉ", "てぃ", "でぃ", "とぅ", "どぅ"]),
]

VOWELS = [("a", "あ"), ("i", "い"), ("u", "う"), ("e", "え"), ("o", "お"), ("n", "ん")]
AVAILABLE_VCV = {
    (left, right)
    for left, _ in VOWELS
    for right, _ in VOWELS
    if (left, right) != ("n", "a")
}

# Representatives span the major Japanese articulation families. A de Bruijn
# order-2 sequence covers every ordered class-to-class join exactly once while
# remaining short enough for routine Docker regression runs.
JOIN_REPRESENTATIVES = [
    "あ", "か", "が", "さ", "ざ", "し", "じ", "た", "だ", "ち", "つ",
    "な", "は", "ふ", "ば", "ぱ", "ま", "ら", "ん",
]


def de_bruijn_indices(alphabet_size: int, order: int) -> list[int]:
    work = [0] * (alphabet_size * order)
    sequence: list[int] = []

    def visit(position: int, period: int) -> None:
        if position > order:
            if order % period == 0:
                sequence.extend(work[1:period + 1])
            return
        work[position] = work[position - period]
        visit(position + 1, period)
        for value in range(work[position - period] + 1, alphabet_size):
            work[position] = value
            visit(position + 1, position)

    visit(1, 1)
    return sequence


def ms_for_tick(tick: int) -> float:
    return tick * 60000.0 / (BPM * TPQ)


def build() -> tuple[list[dict], list[dict], int]:
    notes: list[dict] = []
    boundaries: list[dict] = []
    tick = 0
    phrase_id = 0
    contour = [55, 57, 59, 60, 59, 57, 55]

    def add_phrase(group: str, lyrics: list[str], durations: list[int] | None = None,
                   tones: list[int] | None = None, expected_aliases: list[str | None] | None = None,
                   gaps_after: list[int] | None = None) -> None:
        nonlocal tick, phrase_id
        phrase_id += 1
        local: list[dict] = []
        durations = durations or [240] * len(lyrics)
        tones = tones or [contour[i % len(contour)] for i in range(len(lyrics))]
        expected_aliases = expected_aliases or [None] * len(lyrics)
        gaps_after = gaps_after or [0] * len(lyrics)
        for index, (lyric, duration, tone, expected_alias) in enumerate(
                zip(lyrics, durations, tones, expected_aliases)):
            note = {
                "position": tick,
                "duration": duration,
                "tone": tone,
                "lyric": lyric,
                "group": group,
                "expectedAlias": expected_alias,
                "phraseId": phrase_id,
            }
            notes.append(note)
            local.append(note)
            if index:
                previous = local[index - 1]
                boundaries.append({
                    "tick": tick,
                    "seconds": ms_for_tick(tick) / 1000.0,
                    "group": group,
                    "label": f"{previous['lyric']} to {lyric}",
                    "leftLyric": previous["lyric"],
                    "rightLyric": lyric,
                    "expectedAlias": expected_alias,
                    "leftTone": previous["tone"],
                    "rightTone": tone,
                    "leftDurationTick": previous["duration"],
                    "rightDurationTick": duration,
                })
            tick += duration + gaps_after[index]
        tick += 240

    for row, lyrics in GOJUON:
        add_phrase(f"cv:{row}", lyrics)
    for row, lyrics in CONTRACTED:
        add_phrase(f"contracted:{row}", lyrics)

    # Every vowel/nasal VCV alias shipped in the pinned bank gets an isolated
    # adjacent pair, so selection and positive-overlap behavior are explicit.
    for left_roman, left_kana in VOWELS:
        for right_roman, right_kana in VOWELS:
            available = (left_roman, right_roman) in AVAILABLE_VCV
            add_phrase(
                "vcv-matrix",
                [left_kana, right_kana],
                durations=[240, 240],
                tones=[55, 59],
                expected_aliases=[None, f"{left_roman} {right_kana}" if available else None],
            )

    join_cycle = de_bruijn_indices(len(JOIN_REPRESENTATIVES), 2)
    join_sequence = [JOIN_REPRESENTATIVES[index] for index in join_cycle]
    join_sequence.append(join_sequence[0])
    assert len(join_sequence) == len(JOIN_REPRESENTATIVES) ** 2 + 1
    assert len(set(zip(join_sequence, join_sequence[1:]))) == len(JOIN_REPRESENTATIVES) ** 2
    # Keep each OpenUtau render phrase bounded. Adjacent chunks repeat their
    # shared endpoint, so no ordered edge is lost when the separating rest is
    # inserted.
    join_edges_per_phrase = 40
    for start in range(0, len(join_sequence) - 1, join_edges_per_phrase):
        stop = min(start + join_edges_per_phrase, len(join_sequence) - 1)
        chunk = join_sequence[start:stop + 1]
        add_phrase(
            "join-matrix",
            chunk,
            durations=[240] * len(chunk),
            tones=[55] * len(chunk),
        )

    stress = ["あ", "だ", "ち", "れ", "い", "う"]
    add_phrase("duration:squeezed", stress, durations=[60] * len(stress), tones=[55, 57, 59, 60, 59, 55])
    add_phrase("duration:short", stress, durations=[120] * len(stress), tones=[55, 57, 59, 60, 59, 55])
    add_phrase("duration:long", stress, durations=[960] * len(stress), tones=[55, 57, 59, 60, 59, 55])
    add_phrase("duration:mixed", stress, durations=[60, 120, 240, 480, 960, 240], tones=[55, 57, 59, 60, 59, 55])
    add_phrase("continuation:plus", ["あ", "+", "+~", "+1"],
               durations=[240, 480, 960, 240], tones=[55, 59, 62, 57])
    add_phrase("word-split:adachi-rei", ["あ", "だ", "ち", "れ", "い"],
               durations=[240, 120, 360, 240, 480], tones=[55, 57, 59, 60, 59])
    add_phrase("timing:gapped", stress, durations=[180] * len(stress),
               tones=[55, 57, 59, 60, 59, 55], gaps_after=[60, 120, 60, 240, 60, 0])
    add_phrase("pitch:low", stress, durations=[240] * len(stress), tones=[43, 45, 47, 48, 47, 43])
    add_phrase("pitch:high", stress, durations=[240] * len(stress), tones=[67, 69, 71, 72, 71, 67])
    add_phrase("pitch:octave-low", stress, durations=[480] * len(stress), tones=[31, 33, 35, 36, 35, 31])
    add_phrase("pitch:octave-high", stress, durations=[480] * len(stress), tones=[79, 81, 83, 84, 83, 79])
    add_phrase(
        "pitch:intervals",
        ["あ"] * 9,
        durations=[480] * 9,
        tones=[55, 67, 55, 48, 55, 62, 55, 43, 55],
    )
    return notes, boundaries, tick


def write_ustx(path: pathlib.Path, notes: list[dict], duration: int,
               position_offset: int = 0, title: str = "VocalRack Adachi Rei automated regression corpus") -> None:
    lines = [
        f"name: {title}",
        "comment: Generated by scripts/generate_openutau_regression.py; do not edit by hand.",
        "ustx_version: 0.9",
        "tempos:",
        f"- {{position: 0, bpm: {BPM}}}",
        "time_signatures:",
        "- {bar_position: 0, beat_per_bar: 4, beat_unit: 4}",
        "tracks:",
        "- track_name: Adachi Rei regression",
        "  singer: adachi-rei",
        "  phonemizer: OpenUtau.Plugin.Builtin.JapanesePresampPhonemizer",
        "  renderer_settings:",
        "    renderer: WORLDLINE-R",
        "voice_parts:",
        "- name: coverage corpus",
        "  track_no: 0",
        "  position: 0",
        f"  duration: {duration}",
        "  notes:",
    ]
    for note in notes:
        lines.extend([
            f"  - position: {note['position'] - position_offset}",
            f"    duration: {note['duration']}",
            f"    tone: {note['tone']}",
            f"    lyric: {note['lyric']}",
            "    pitch:",
            "      snap_first: true",
            "      data:",
            "      - {x: -40, y: 0, shape: io}",
            "      - {x: 40, y: 0, shape: io}",
            "    vibrato: {length: 0, period: 185, depth: 0, in: 10, out: 10, shift: 0}",
        ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=pathlib.Path)
    args = parser.parse_args()
    notes, boundaries, duration = build()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_ustx(args.output_dir / "corpus.ustx", notes, duration)

    # OpenUtau's headless Classic exporter can return an empty mix for very
    # large synthetic parts. Shard only at existing phrase/rest boundaries and
    # later place each render at its exact global start, preserving every join.
    phrases: list[list[dict]] = []
    for phrase_id in sorted({note["phraseId"] for note in notes}):
        phrases.append([note for note in notes if note["phraseId"] == phrase_id])
    shard_note_groups: list[list[dict]] = []
    current: list[dict] = []
    maximum_shard_ticks = 48000  # 50 seconds at 120 BPM.
    for phrase in phrases:
        candidate = current + phrase
        candidate_start = candidate[0]["position"]
        candidate_end = candidate[-1]["position"] + candidate[-1]["duration"]
        if current and candidate_end - candidate_start > maximum_shard_ticks:
            shard_note_groups.append(current)
            current = list(phrase)
        else:
            current = candidate
    if current:
        shard_note_groups.append(current)

    shards = []
    for index, shard_notes in enumerate(shard_note_groups, start=1):
        start = shard_notes[0]["position"]
        end = shard_notes[-1]["position"] + shard_notes[-1]["duration"]
        input_name = f"corpus-shard-{index:02d}.ustx"
        output_name = f"classic-shard-{index:02d}.wav"
        write_ustx(
            args.output_dir / input_name, shard_notes, end - start,
            position_offset=start,
            title=f"VocalRack Adachi Rei regression shard {index}")
        shards.append({
            "input": input_name,
            "output": output_name,
            "startTick": start,
            "startSeconds": ms_for_tick(start) / 1000.0,
            "durationSeconds": ms_for_tick(end - start) / 1000.0,
            "notes": len(shard_notes),
        })
    (args.output_dir / "corpus-shards.json").write_text(
        json.dumps({"sampleRate": 44100, "durationSeconds": ms_for_tick(duration) / 1000.0,
                    "shards": shards}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    (args.output_dir / "corpus-shards.txt").write_text(
        "".join(f"{shard['input']} {shard['output']}\n" for shard in shards),
        encoding="utf-8")
    manifest = {
        "schemaVersion": 1,
        "bpm": BPM,
        "ticksPerQuarter": TPQ,
        "notes": len(notes),
        "noteEvents": [
            {
                "seconds": ms_for_tick(note["position"]) / 1000.0,
                "durationSeconds": ms_for_tick(note["duration"]) / 1000.0,
                "tone": note["tone"],
                "lyric": note["lyric"],
                "group": note["group"],
            }
            for note in notes
        ],
        "boundaries": boundaries,
        "durationSeconds": ms_for_tick(duration) / 1000.0,
        "coverage": {
            "gojuonRows": len(GOJUON),
            "contractedRows": len(CONTRACTED),
            "vcvMatrixTransitions": len(VOWELS) ** 2,
            "availableVcvAliases": len(AVAILABLE_VCV),
            "documentedVcvFallbacks": len(VOWELS) ** 2 - len(AVAILABLE_VCV),
            "stressGroups": 12,
            "joinMatrixSymbols": len(JOIN_REPRESENTATIVES),
            "joinMatrixOrderedPairs": len(JOIN_REPRESENTATIVES) ** 2,
        },
    }
    (args.output_dir / "corpus-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"notes": len(notes), "boundaries": len(boundaries),
                      "seconds": manifest["durationSeconds"], "shards": len(shards)}, indent=2))


if __name__ == "__main__":
    main()
