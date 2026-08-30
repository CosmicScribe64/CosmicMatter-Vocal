#!/usr/bin/env python3
"""Generate the shipped Rack 2 demo patch archive."""

import json
import pathlib
import subprocess
import sys
import tarfile


def vocal(module_id: int, x: int, transpose: int) -> dict:
    values = [0.0, 0.0, 1.0, 0.0, 120.0, float(transpose), 0.0, 0.0, 0.0, 0.0, 0.0]
    return {
        "id": module_id, "plugin": "CosmicMatter", "model": "Vocal", "version": "2.0.0",
        "params": [{"id": i, "value": value} for i, value in enumerate(values)],
        "data": {"singerId": "builtin:adachi-rei", "phonemizer": "Japanese Auto", "ppqn": 24,
                 "runRisingBehavior": 0, "sectionQuantization": 3, "panelPlaying": True},
        "pos": [x, 0],
    }


def main() -> None:
    output = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "patches/VocalRack-Demo.vcv").resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    patch = {
        "version": "2.6.6", "zoom": 0.45, "gridOffset": [0.0, 0.0],
        "modules": [
            vocal(1, 0, 0), vocal(2, 64, 7),
            {"id": 3, "plugin": "CosmicMatter", "model": "SingerPlate", "version": "2.0.0", "params": [],
             "data": {"singerId": "builtin:adachi-rei", "fitMode": 0, "showName": True}, "pos": [128, 0]},
        ],
        "cables": [],
    }
    build = output.parent / ".demo-patch-build"
    build.mkdir(exist_ok=True); (build / "modules").mkdir(exist_ok=True)
    (build / "patch.json").write_text(json.dumps(patch, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tar_path = output.with_suffix(".tar")
    with tarfile.open(tar_path, "w") as archive:
        archive.add(build / "patch.json", arcname="patch.json")
        archive.add(build / "modules", arcname="modules")
    subprocess.run(["zstd", "--quiet", "--force", str(tar_path), "-o", str(output)], check=True)
    tar_path.unlink()


if __name__ == "__main__":
    main()
