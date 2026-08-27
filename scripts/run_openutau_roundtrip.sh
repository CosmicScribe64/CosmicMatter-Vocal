#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir="$repo_dir/test-artifacts/roundtrip"
image=${OPENUTAU_DOTNET_IMAGE:-mcr.microsoft.com/dotnet/sdk:8.0}

"$repo_dir/scripts/ensure_openutau_reference.sh"
mkdir -p "$output_dir"
make -f "$repo_dir/tests/Makefile" -C "$repo_dir" build-tests/vocalrack-render
"$repo_dir/build-tests/vocalrack-render" \
  --phonemizer "Japanese Auto" \
  --score "$repo_dir/tests/fixtures/rei_phrase.json" \
  --export-ustx "$output_dir/exported-rei-phrase.ustx" \
  > "$output_dir/vocalrack-export.log" 2>&1

docker run --rm \
  -v "$repo_dir:/workspace:ro" \
  -v "$output_dir:/out" \
  "$image" \
  sh -eu -c '
    mkdir -p /tmp/workspace/reference /tmp/workspace/tools /tmp/ou-data/OpenUtau/Singers /tmp/ou-cache/OpenUtau
    cp -a /workspace/reference/OpenUtau /tmp/workspace/reference/OpenUtau
    cp -a /workspace/tools/openutau-compare /tmp/workspace/tools/openutau-compare
    ln -s /workspace/res/singers/adachi-rei /tmp/ou-data/OpenUtau/Singers/adachi-rei
    cd /tmp/workspace/tools/openutau-compare
    dotnet publish -c Release -o /tmp/ou-publish -v:q -p:WarningLevel=0
    case "$(uname -m)" in
      aarch64|arm64) native=linux-arm64 ;;
      *) native=linux-x64 ;;
    esac
    cp "/tmp/workspace/reference/OpenUtau/runtimes/$native/native/libworldline.so" /tmp/ou-publish/
    XDG_DATA_HOME=/tmp/ou-data XDG_CACHE_HOME=/tmp/ou-cache LD_LIBRARY_PATH=/tmp/ou-publish \
      dotnet /tmp/ou-publish/OpenUtauCompare.dll \
      /out/exported-rei-phrase.ustx /out/openutau-classic.wav CLASSIC \
      > /out/openutau-classic.log 2>&1
  '

"$repo_dir/build-tests/vocalrack-render" \
  --phonemizer "Japanese Auto" \
  --singer "$repo_dir/res/singers/adachi-rei" \
  --ustx "$output_dir/exported-rei-phrase.ustx" --track 0 --sample-rate 44100 \
  --out "$output_dir/vocalrack-reimport.wav" \
  > "$output_dir/vocalrack-reimport.log" 2>&1

python3 "$repo_dir/scripts/write_roundtrip_report.py" "$output_dir"
printf 'VocalRack/OpenUtau round-trip evidence written to %s\n' "$output_dir"
