#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir="$repo_dir/test-artifacts/openutau-english-regression"
dotnet_image=${OPENUTAU_DOTNET_IMAGE:-mcr.microsoft.com/dotnet/sdk:8.0}
analysis_image=${VOCALRACK_ANALYSIS_IMAGE:-vocalrack-audio-analysis:py3.12}

"$repo_dir/scripts/ensure_openutau_reference.sh"
mkdir -p "$output_dir"
python3 "$repo_dir/scripts/generate_openutau_english_regression.py" "$output_dir"

docker run --rm \
  -v "$repo_dir:/workspace:ro" \
  -v "$output_dir:/out" \
  "$dotnet_image" \
  sh -eu -c '
    mkdir -p /tmp/workspace/reference /tmp/workspace/tools /tmp/ou-data/OpenUtau/Singers /tmp/ou-cache/OpenUtau
    cp -a /workspace/reference/OpenUtau /tmp/workspace/reference/OpenUtau
    cp -a /workspace/tools/openutau-compare /tmp/workspace/tools/openutau-compare
    ln -s /workspace/res/singers/adachi-rei /tmp/ou-data/OpenUtau/Singers/adachi-rei
    ln -s /out/xsampa/voicebank /tmp/ou-data/OpenUtau/Singers/vocalrack-en-xsampa
    ln -s /out/vccv/voicebank /tmp/ou-data/OpenUtau/Singers/vocalrack-en-vccv
    cd /tmp/workspace/tools/openutau-compare
    dotnet publish -c Release -o /tmp/ou-publish -v:q -p:WarningLevel=0
    case "$(uname -m)" in
      aarch64|arm64) native=linux-arm64 ;;
      *) native=linux-x64 ;;
    esac
    cp "/tmp/workspace/reference/OpenUtau/runtimes/$native/native/libworldline.so" /tmp/ou-publish/
    for suite in adachi xsampa vccv; do
      XDG_DATA_HOME=/tmp/ou-data XDG_CACHE_HOME=/tmp/ou-cache LD_LIBRARY_PATH=/tmp/ou-publish \
        dotnet /tmp/ou-publish/OpenUtauCompare.dll "/out/$suite/corpus.ustx" \
        "/out/$suite/openutau-classic.wav" CLASSIC > "/out/$suite/openutau-classic.log" 2>&1
    done
  '

make -f "$repo_dir/tests/Makefile" -C "$repo_dir" build-tests/vocalrack-render
"$repo_dir/build-tests/vocalrack-render" \
  --phonemizer "English to Japanese" --singer "$repo_dir/res/singers/adachi-rei" \
  --ustx "$output_dir/adachi/corpus.ustx" --sample-rate 44100 \
  --out "$output_dir/adachi/vocalrack.wav" > "$output_dir/adachi/vocalrack.log" 2>&1
"$repo_dir/build-tests/vocalrack-render" \
  --phonemizer "EN X-SAMPA" --singer "$output_dir/xsampa/voicebank" \
  --ustx "$output_dir/xsampa/corpus.ustx" --sample-rate 44100 \
  --out "$output_dir/xsampa/vocalrack.wav" > "$output_dir/xsampa/vocalrack.log" 2>&1
"$repo_dir/build-tests/vocalrack-render" \
  --phonemizer "English VCCV" --singer "$output_dir/vccv/voicebank" \
  --ustx "$output_dir/vccv/corpus.ustx" --sample-rate 44100 \
  --out "$output_dir/vccv/vocalrack.wav" > "$output_dir/vccv/vocalrack.log" 2>&1

docker build -q -t "$analysis_image" "$repo_dir/tools/audio-analysis" >/dev/null
for suite in adachi xsampa vccv; do
  case "$suite" in
    adachi) label="Adachi Rei English to Japanese"; singer_id="adachi-rei" ;;
    xsampa) label="English X-SAMPA"; singer_id="vocalrack-en-xsampa" ;;
    vccv) label="English VCCV"; singer_id="vocalrack-en-vccv" ;;
  esac
  docker run --rm --entrypoint python3 \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp/analysis-home \
    -e MPLCONFIGDIR=/tmp/analysis-home/matplotlib \
    -e NUMBA_CACHE_DIR=/tmp/analysis-home/numba \
    -v "$repo_dir:/workspace:ro" \
    -v "$output_dir:/out" \
    "$analysis_image" \
    /workspace/scripts/analyze_openutau_regression.py \
    "/out/$suite/vocalrack.wav" "/out/$suite/openutau-classic.wav" \
    "/out/$suite/corpus-manifest.json" "/out/$suite/vocalrack.log" \
    "/out/$suite/openutau-classic.log" \
    /workspace/tests/fixtures/openutau_english_thresholds.json "/out/$suite" \
    --label "$label" --expected-singer-id "$singer_id"
done

python3 "$repo_dir/scripts/write_openutau_english_report.py" \
  "$output_dir/adachi/regression-report.json" \
  "$output_dir/xsampa/regression-report.json" \
  "$output_dir/vccv/regression-report.json" \
  "$output_dir/TEST_REPORT.md"
printf 'English OpenUtau regression evidence written to %s\n' "$output_dir"
