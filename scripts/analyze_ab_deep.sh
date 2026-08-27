#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
vocalrack_rel=${1:-test-artifacts/openutau-ab/vocalrack.wav}
openutau_rel=${2:-test-artifacts/openutau-ab/openutau.wav}
output_rel=${3:-test-artifacts/openutau-ab/deep-analysis}
comparison_label=${4:-OpenUtau}
image=${VOCALRACK_ANALYSIS_IMAGE:-vocalrack-audio-analysis:py3.12}

mkdir -p "$repo_dir/$output_rel"
docker build -q -t "$image" "$repo_dir/tools/audio-analysis" >/dev/null
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/analysis-home \
  -e MPLCONFIGDIR=/tmp/analysis-home/matplotlib \
  -e NUMBA_CACHE_DIR=/tmp/analysis-home/numba \
  -v "$repo_dir:/workspace:ro" \
  -v "$repo_dir/$output_rel:/out" \
  "$image" \
  "/workspace/$vocalrack_rel" "/workspace/$openutau_rel" \
  /workspace/tests/fixtures/adachi_rei_ab.ustx /out "$comparison_label"

printf 'Deep audio analysis written to %s\n' "$repo_dir/$output_rel"
