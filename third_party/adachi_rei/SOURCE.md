# Adachi Rei voicebank provenance

- Official source page: https://mechanicalgirl.jp/adachi-rei/
- Exact archive URL: https://www.dropbox.com/scl/fi/8z9corxo32gwgqyv5z92a/ver3.5.0.zip?rlkey=53n6e60cozvp3c9nvwyz6odxx&dl=1
- Voicebank: 足立レイ (Adachi Rei), standard UTAU voicebank, version 3.5.0
- Download date: 2026-08-26
- Original archive SHA-256: `b96d1b21145f22e573afd9ec8aeaad0ec9cbaee581c2623c64addeb31de46b3d`
- Official guidelines: https://mechanicalgirl.jp/guidelines/
- License/terms files present in the archive: `readme.txt` (UTF-16LE). The archive readme refers users to the official guidelines; no separately named guideline file was present in this exact archive.

## Files shipped

The release ships the archive contents required for rendering and attribution under `res/singers/adachi-rei/`: WAV samples, frequency metadata, `oto.ini`, `character.txt`, `character.yaml`, `Rei.bmp`, `Adachi Rei.png`, `readme.txt`, and the other files supplied in the official archive. `scripts/validate_release.sh` verifies the required subset.

## Transformations

The original ZIP remains unchanged in the controlled, git-ignored `development-assets/` directory. Release files were extracted with libarchive using CP932 for ZIP entry names and the single top-level `足立レイver3.5.0` directory was stripped. File contents were not transcoded, edited, normalized, or relicensed. In particular, decomposed Unicode filenames from the archive remain decomposed.

VocalRack source code is MIT licensed. The bundled Adachi Rei UTAU voicebank and associated character assets remain under the original Mechanical Girl terms.

