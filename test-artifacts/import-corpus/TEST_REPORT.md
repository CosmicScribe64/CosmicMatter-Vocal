# Real-world score import probe

Validation date: 2026-08-26

`scripts/test_import_corpus.py` ran the production C++ importer against two song-scale inputs:

- Repository fixture `tests/fixtures/legacy_song.ust`: one 16-note legacy UTAU track with a rest, Mode2 pitch data, intensity, vibrato, short/long notes, and `+`, `-`, and `ー` vowel continuations.
- A 436 KiB community OpenUtau project linked from [OpenUtau issue #2104](https://github.com/openutau/OpenUtau/issues/2104), downloaded only to `/private/tmp` and not redistributed. Four non-empty vocal tracks containing 378, 342, 92, and 91 notes (903 total) imported successfully at 143 BPM. Eight empty/non-vocal tracks were omitted from the track chooser.

Machine-readable output is in `community-result.json`. This probe proves structural import and validation; it does not claim that the bundled Japanese Adachi Rei bank can pronounce an English project. Correct synthesis requires a voicebank and phonemizer with aliases that match the score language. The import report lists unsupported singer, phonemizer, and renderer selections that Vocal did not adopt.

Reproduce against any downloaded corpus without adding it to the repository:

```sh
make import-corpus CORPUS=/path/to/file-or-folder
```
