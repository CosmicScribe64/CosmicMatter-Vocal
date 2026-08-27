#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_dir="$repo_dir/test-artifacts/openutau-regression"
dotnet_image=${OPENUTAU_DOTNET_IMAGE:-mcr.microsoft.com/dotnet/sdk:8.0}
analysis_image=${VOCALRACK_ANALYSIS_IMAGE:-vocalrack-audio-analysis:py3.12}

"$repo_dir/scripts/ensure_openutau_reference.sh"
mkdir -p "$output_dir"
python3 "$repo_dir/scripts/generate_openutau_regression.py" "$output_dir"

docker run --rm \
  -v "$repo_dir:/workspace:ro" \
  -v "$output_dir:/out" \
  "$dotnet_image" \
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
    : > /out/openutau-classic.log
    while read -r shard_input shard_output; do
      printf "OpenUtau regression shard: %s\n" "$shard_input" >> /out/openutau-classic.log
      XDG_DATA_HOME=/tmp/ou-data XDG_CACHE_HOME=/tmp/ou-cache LD_LIBRARY_PATH=/tmp/ou-publish \
        dotnet /tmp/ou-publish/OpenUtauCompare.dll "/out/$shard_input" "/out/$shard_output" CLASSIC \
        >> /out/openutau-classic.log 2>&1
    done < /out/corpus-shards.txt
    XDG_DATA_HOME=/tmp/ou-data XDG_CACHE_HOME=/tmp/ou-cache LD_LIBRARY_PATH=/tmp/ou-publish \
      dotnet /tmp/ou-publish/OpenUtauCompare.dll \
      /workspace/tests/fixtures/adachi_rei_expression.ustx \
      /out/openutau-expression-classic.wav CLASSIC \
      > /out/openutau-expression-classic.log 2>&1
  '

make -f "$repo_dir/tests/Makefile" -C "$repo_dir" build-tests/vocalrack-render
"$repo_dir/build-tests/vocalrack-render" \
  --phonemizer "Japanese Auto" \
  --singer "$repo_dir/res/singers/adachi-rei" \
  --ustx "$output_dir/corpus.ustx" --track 0 --sample-rate 44100 \
  --out "$output_dir/vocalrack.wav" > "$output_dir/vocalrack.log" 2>&1
"$repo_dir/build-tests/vocalrack-render" \
  --phonemizer "Japanese Auto" \
  --singer "$repo_dir/res/singers/adachi-rei" \
  --ustx "$repo_dir/tests/fixtures/adachi_rei_expression.ustx" --track 0 --sample-rate 44100 \
  --out "$output_dir/vocalrack-expression.wav" > "$output_dir/vocalrack-expression.log" 2>&1

docker build -q -t "$analysis_image" "$repo_dir/tools/audio-analysis" >/dev/null
docker run --rm --entrypoint python3 \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/analysis-home \
  -v "$repo_dir:/workspace:ro" \
  -v "$output_dir:/out" \
  "$analysis_image" \
  /workspace/scripts/assemble_openutau_shards.py \
  /out/corpus-shards.json /out/openutau-classic.wav
docker run --rm --entrypoint python3 \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/analysis-home \
  -e MPLCONFIGDIR=/tmp/analysis-home/matplotlib \
  -e NUMBA_CACHE_DIR=/tmp/analysis-home/numba \
  -v "$repo_dir:/workspace:ro" \
  -v "$output_dir:/out" \
  "$analysis_image" \
  /workspace/scripts/analyze_openutau_regression.py \
  /out/vocalrack.wav /out/openutau-classic.wav /out/corpus-manifest.json \
  /out/vocalrack.log /out/openutau-classic.log \
  /workspace/tests/fixtures/openutau_regression_thresholds.json /out
docker run --rm --entrypoint python3 \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/analysis-home \
  -e MPLCONFIGDIR=/tmp/analysis-home/matplotlib \
  -e NUMBA_CACHE_DIR=/tmp/analysis-home/numba \
  -v "$repo_dir:/workspace:ro" \
  -v "$output_dir:/out" \
  "$analysis_image" \
  /workspace/scripts/analyze_openutau_expression.py \
  /out/vocalrack-expression.wav /out/openutau-expression-classic.wav \
  /workspace/tests/fixtures/adachi_rei_expression_manifest.json \
  /workspace/tests/fixtures/openutau_expression_thresholds.json /out
python3 "$repo_dir/scripts/write_openutau_regression_report.py" \
  "$output_dir/regression-report.json" "$output_dir/expression-report.json" \
  "$output_dir/TEST_REPORT.md"

printf 'OpenUtau regression evidence written to %s\n' "$output_dir"
