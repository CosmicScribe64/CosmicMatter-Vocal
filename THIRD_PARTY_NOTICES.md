# Third-party notices

## Adachi Rei UTAU voicebank

The bundled 足立レイ (Adachi Rei) UTAU voicebank version 3.5.0 and its associated character assets are by Mechanical Girl / Missile 39 and are **not** licensed under VocalRack's MIT license. They remain under the original terms included in `res/singers/adachi-rei/readme.txt` and published at https://mechanicalgirl.jp/guidelines/. Redistribution and modification of the UTAU voice source are permitted by those official terms; users remain responsible for complying with them. Exact provenance and archive hash are in `third_party/adachi_rei/SOURCE.md`.

## nlohmann/json

VocalRack includes nlohmann/json 3.11.3, copyright © 2013-2022 Niels Lohmann, under the MIT License. The license is retained at `third_party/nlohmann/LICENSE.MIT`.

## Lucide tool icons

The score editor's select, pencil, eraser, and scissors glyphs are from Lucide Icons, copyright © 2026 Lucide Icons and Contributors, under the ISC License. The license is retained at `third_party/lucide/LICENSE`.

## CMU Pronouncing Dictionary

The packaged `res/dictionaries/cmudict-0.7b.txt` is CMUdict 0.7b, copyright © 1993-2015 Carnegie Mellon University. It is redistributed under the permissive license printed at the top of that file. Vocal uses it for deterministic English word-to-ARPAbet lookup. Bracketed X-SAMPA aliases and a small product-name lexicon cover words outside the dictionary.

## OpenUtau reference

Development scripts clone OpenUtau at commit `8bef4418feb253d06e8b39956298367294afcf06` into the git-ignored `reference/OpenUtau` directory. OpenUtau is MIT licensed, copyright © 2014 StAkira. VocalRack V1 contains no OpenUtau source files. The implementation uses OpenUtau as a behavioral reference for voicebank loading, Japanese VCV selection, render-phrase timing, and USTX data.
