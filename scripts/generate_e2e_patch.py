#!/usr/bin/env python3
"""Create the binary Rack 2 E2E patch and its small external image-bank fixture."""

import json
import os
import pathlib
import struct
import subprocess
import sys
import tarfile


def write_test_bmp(path: pathlib.Path) -> None:
    width, height = 160, 220
    row_bytes = (width * 3 + 3) & ~3
    pixels = bytearray(row_bytes * height)
    for y in range(height):
        for x in range(width):
            # An unmistakably non-Adachi geometric cyan/orange test portrait.
            r = 238 if (x // 20 + y // 20) % 2 else 35
            g = 125 if x < width // 2 else 210
            b = 55 if y < height // 2 else 225
            offset = y * row_bytes + x * 3
            pixels[offset:offset + 3] = bytes((b, g, r))
    header = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
    dib = struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    path.write_bytes(header + dib + pixels)


def vocal(module_id: int, x: int, y: int, loop: float, range_mode: float, transpose: float,
          score: dict | None = None, attenuverters: tuple[float, float, float, float] = (0, 0, 0, 0),
          phonemizer: str = "Japanese Auto") -> dict:
    values = [0.0, 0.0, loop, range_mode, 120.0, transpose, 0.0, *attenuverters]
    data = {
        "stateSchemaVersion": 1,
        "singerId": "builtin:adachi-rei",
        "externalSingerPath": "",
        "phonemizer": phonemizer,
        "ppqn": 24,
        "runRisingBehavior": 0,
        "sectionQuantization": 3,
        "panelPlaying": False,
        "editorScrollX": 0.0,
        "editorScrollY": 60.0,
        "editorZoomX": 0.16,
        "editorZoomY": 12.0,
    }
    if score is not None:
        data["score"] = json.dumps(score, ensure_ascii=False, separators=(",", ":"))
    return {
        "id": module_id,
        "plugin": "CosmicMatter-Vocal",
        "model": "Vocal",
        "version": "2.0.0",
        "params": [{"id": i, "value": value} for i, value in enumerate(values)],
        "data": data,
        "pos": [x, y],
    }


def plateau(module_id: int, x: int, y: int) -> dict:
    # Wet-only Valley Plateau. Its signal is captured separately from the dry
    # VocalRack output so the validator can prove a changed signal and tail.
    values = [0.0, 0.12, 0.0, 10.0, 8.0, 0.55, 10.0, 0.72, 10.0, 7.0,
              0.08, 0.5, 0.35, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    return {"id": module_id, "plugin": "Valley", "model": "Plateau", "version": "2.4.5",
            "params": [{"id": i, "value": value} for i, value in enumerate(values)],
            "data": {"frozen": False, "freezeToggle": False, "panelStyle": 0, "tuned": 0,
                     "diffuseInput": 1, "preDelayCVSens": 0, "inputSensitivity": 0,
                     "outputSaturation": 0}, "pos": [x, y]}


def adsr(module_id: int, x: int, y: int) -> dict:
    values = [0.35, 0.35, 0.82, 0.60, 0.0, 0.0, 0.0, 0.0, 0.0]
    return {"id": module_id, "plugin": "Fundamental", "model": "ADSR", "version": "2.6.4",
            "params": [{"id": i, "value": value} for i, value in enumerate(values)], "data": {}, "pos": [x, y]}


def vca(module_id: int, x: int, y: int) -> dict:
    return {"id": module_id, "plugin": "Fundamental", "model": "VCA", "version": "2.6.4",
            "params": [{"id": 0, "value": 1.0}, {"id": 1, "value": 1.0}], "data": {}, "pos": [x, y]}


def cable(cable_id: int, output_module: int, output_id: int, input_module: int, input_id: int, color: str) -> dict:
    return {
        "id": cable_id,
        "outputModuleId": output_module,
        "outputId": output_id,
        "inputModuleId": input_module,
        "inputId": input_id,
        "color": color,
    }


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_e2e_patch.py OUTPUT.vcv EXTERNAL_BANK_DIR")
    output = pathlib.Path(sys.argv[1]).resolve()
    bank = pathlib.Path(sys.argv[2]).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    bank.mkdir(parents=True, exist_ok=True)
    (bank / "character.txt").write_text("name=E2E Test Singer\nimage=test.bmp\nauthor=VocalRack tests\n", encoding="utf-8")
    (bank / "oto.ini").write_text("", encoding="utf-8")
    write_test_bmp(bank / "test.bmp")

    repo = pathlib.Path(__file__).resolve().parents[1]
    fixture = lambda name: json.loads((repo / "tests" / "fixtures" / name).read_text(encoding="utf-8"))

    modules = [
        # The factory score contains English words. Exercise the
        # bundled English-to-Japanese path instead of forcing those words
        # through the Japanese-only phonemizer (which would correctly leave
        # unresolved spans silent).
        vocal(1, 0, 0, 0.0, 0.0, 0.0, phonemizer="English to Japanese"),
        vocal(2, 64, 0, 1.0, 0.0, 7.0, phonemizer="English to Japanese"),
        vocal(3, 128, 0, 1.0, 1.0, 0.0, fixture("phrase_bank.json")),
        vocal(4, 0, 1, 0.0, 1.0, 0.0, fixture("one_shot.json")),
        vocal(5, 64, 1, 0.0, 0.0, 0.0, fixture("long_multibar.json")),
        vocal(6, 128, 1, 1.0, 1.0, 0.0, fixture("drone.json"), (0.20, 0.25, 0.30, 0.50)),
        {"id": 7, "plugin": "CosmicMatter-Vocal", "model": "SingerPlate", "version": "2.0.0", "params": [],
         "data": {"singerId": "builtin:adachi-rei", "externalSingerPath": "", "fitMode": 0, "showName": True}, "pos": [192, 0]},
        {"id": 8, "plugin": "CosmicMatter-Vocal", "model": "SingerPlate", "version": "2.0.0", "params": [],
         "data": {"singerId": "external:e2e-test-bank", "externalSingerPath": str(bank), "fitMode": 1, "showName": True}, "pos": [204, 0]},
        {"id": 10, "plugin": "VocalRackE2E", "model": "E2EDriver", "version": "2.0.0", "params": [],
         "data": {"outputDirectory": str(output.parents[2])}, "pos": [216, 0]},
        adsr(20, 192, 1), vca(21, 204, 1), plateau(30, 210, 1), plateau(31, 222, 1),
    ]
    cables = []
    cid = 1
    colors = ["#c91847", "#0c8e15", "#0986ad", "#f0b000"]
    for target in (1, 2, 3, 4, 5, 6):
        cables.append(cable(cid, 10, 0, target, 0, colors[cid % len(colors)])); cid += 1
    links = [
        (10, 1, 1, 2), (10, 2, 1, 1), (10, 3, 1, 3),
        (10, 5, 2, 2), (10, 6, 2, 1),
        (10, 7, 3, 2), (10, 8, 3, 1), (10, 9, 3, 3), (10, 4, 3, 8),
        (10, 1, 4, 2), (10, 2, 4, 1), (10, 3, 4, 3),
        (10, 1, 5, 2), (10, 2, 5, 1), (10, 3, 5, 3),
        (10, 5, 6, 2), (10, 6, 6, 1),
        (10, 11, 6, 4), (10, 11, 6, 5), (10, 11, 6, 6), (10, 11, 6, 7),
        (10, 10, 20, 4), (20, 0, 21, 1), (6, 0, 21, 2),
        (5, 0, 30, 0), (21, 0, 31, 0),
        (1, 0, 10, 0), (1, 1, 10, 1), (2, 0, 10, 2), (2, 1, 10, 3), (3, 0, 10, 4), (3, 1, 10, 5),
        (4, 0, 10, 6), (4, 1, 10, 7), (5, 0, 10, 8), (5, 1, 10, 9), (30, 0, 10, 10),
        (6, 0, 10, 11), (21, 0, 10, 12), (31, 0, 10, 13),
    ]
    for out_module, out_id, in_module, in_id in links:
        cables.append(cable(cid, out_module, out_id, in_module, in_id, colors[cid % len(colors)])); cid += 1

    patch = {"version": "2.6.6", "zoom": 0.33, "gridOffset": [0.0, 0.0], "modules": modules, "cables": cables}
    work = output.parent / (output.stem + "-patch-build")
    work.mkdir(parents=True, exist_ok=True)
    (work / "modules").mkdir(exist_ok=True)
    (work / "patch.json").write_text(json.dumps(patch, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tar_path = output.with_suffix(".tar")
    with tarfile.open(tar_path, "w") as archive:
        archive.add(work / "patch.json", arcname="patch.json")
        archive.add(work / "modules", arcname="modules")
    subprocess.run(["zstd", "--quiet", "--force", str(tar_path), "-o", str(output)], check=True)
    tar_path.unlink()


if __name__ == "__main__":
    main()
