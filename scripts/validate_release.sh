#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"

fail() { printf 'release validation failed: %s\n' "$1" >&2; exit 1; }

for file in LICENSE README.md MANUAL.md ARCHITECTURE.md IMPLEMENTATION_STATUS.md THIRD_PARTY_NOTICES.md CHANGELOG.md \
    test-artifacts/e2e/TEST_REPORT.md \
    third_party/adachi_rei/SOURCE.md third_party/nlohmann/LICENSE.MIT reference/OPENUTAU_COMMIT.txt \
    res/Vocal.svg res/SingerPlate.svg res/singers/adachi-rei/character.txt res/singers/adachi-rei/readme.txt \
    res/dictionaries/cmudict-0.7b.txt \
    patches/VocalRack-Demo.vcv; do
    test -f "$file" || fail "missing $file"
done

grep -Eq '"slug"[[:space:]]*:[[:space:]]*"CosmicMatter-Vocal"' plugin.json \
    || fail "plugin.json has the wrong permanent plugin slug"
grep -Eq '"license"[[:space:]]*:[[:space:]]*"MIT"' plugin.json \
    || fail "plugin.json license is not MIT"
grep -Eq '"version"[[:space:]]*:[[:space:]]*"2\.[0-9]+\.[0-9]+"' plugin.json \
    || fail "plugin.json version does not target Rack 2"
test "$(grep -Ec '"slug"[[:space:]]*:[[:space:]]*"(Vocal|SingerPlate)"' plugin.json)" -eq 2 \
    || fail "plugin.json module set is invalid"
grep -Fq 'https://github.com/CosmicScribe64/CosmicMatter-Vocal' plugin.json \
    || fail "plugin.json source URLs are not configured"
grep -Eq '"tags"[[:space:]]*:[[:space:]]*\[[^]]*"Speech"' plugin.json \
    || fail "Vocal module does not use the official Speech tag"

oto_count=$(find res/singers/adachi-rei -name oto.ini -type f | wc -l | tr -d ' ')
wav_count=$(find res/singers/adachi-rei -iname '*.wav' -type f | wc -l | tr -d ' ')
test "$oto_count" -ge 1 || fail "bundled singer has no oto.ini"
test "$wav_count" -ge 100 || fail "bundled singer WAV set is incomplete ($wav_count files)"
test -f 'res/singers/adachi-rei/Adachi Rei.png' || test -f res/singers/adachi-rei/Rei.bmp || fail "bundled singer portrait missing"

expected=b96d1b21145f22e573afd9ec8aeaad0ec9cbaee581c2623c64addeb31de46b3d
grep -Fq "$expected" third_party/adachi_rei/SOURCE.md || fail "voicebank hash absent from provenance"
grep -Eq '^8bef4418feb253d06e8b39956298367294afcf06$' reference/OPENUTAU_COMMIT.txt || fail "unexpected OpenUtau reference commit"
cmudict_count=$(grep -Ec "^[A-Z0-9()'_-]+  " res/dictionaries/cmudict-0.7b.txt)
test "$cmudict_count" -ge 125000 || fail "packaged CMUdict is incomplete ($cmudict_count entries)"
grep -Eq '^;;; # CMUdict' res/dictionaries/cmudict-0.7b.txt || fail "CMUdict license/provenance header is missing"

if test -n "${VCVPLUGIN_PATH:-}"; then
    test -f "$VCVPLUGIN_PATH" || fail "package does not exist: $VCVPLUGIN_PATH"
    listing=$(mktemp)
    zstd -d -c "$VCVPLUGIN_PATH" | tar -tf - > "$listing"
    for item in plugin.json LICENSE README.md MANUAL.md ARCHITECTURE.md THIRD_PARTY_NOTICES.md CHANGELOG.md \
        IMPLEMENTATION_STATUS.md test-artifacts/e2e/TEST_REPORT.md \
        res/dictionaries/cmudict-0.7b.txt \
        res/singers/adachi-rei/character.txt res/singers/adachi-rei/readme.txt \
        third_party/adachi_rei/SOURCE.md patches/VocalRack-Demo.vcv; do
        grep -Fq "$item" "$listing" || fail "package missing $item"
    done
fi

printf 'release assets valid: %s oto.ini files, %s WAV files, %s CMUdict entries\n' \
    "$oto_count" "$wav_count" "$cmudict_count"
