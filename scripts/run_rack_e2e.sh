#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
evidence_dir="$repo_dir/test-artifacts/e2e"
run_id=$(date +%Y%m%d-%H%M%S)-$$
runtime_dir="$evidence_dir/runtime/$run_id"
user_dir="$runtime_dir/user"
patch_path="$runtime_dir/VocalRack-E2E.vcv"
rack_bin=${RACK_BIN:-/Applications/VCV Rack 2 Free.app/Contents/MacOS/Rack}

test -x "$rack_bin" || { printf 'VCV Rack executable not found: %s\n' "$rack_bin" >&2; exit 1; }
test "$(uname -s)" = Darwin || { printf 'The reference GUI E2E script currently targets macOS.\n' >&2; exit 1; }

cd "$repo_dir"
make dist
make -C tests/e2e-plugin RACK_DIR="$repo_dir/.rack-sdk/Rack-SDK" dist
production_package=$(find dist -name 'CosmicMatter-Vocal-*.vcvplugin' -type f -print -quit)
helper_package=$(find tests/e2e-plugin/dist -name 'VocalRackE2E-*.vcvplugin' -type f -print -quit)
VCVPLUGIN_PATH="$production_package" sh scripts/validate_release.sh

mkdir -p "$user_dir/plugins-mac-arm64" "$runtime_dir" "$evidence_dir"

# The deep journey keeps signal conditioning visible in the patch through
# ordinary Rack modules: VCV Free ADSR/VCA and Valley Plateau.
rack_library_plugins=${RACK_LIBRARY_PLUGINS:-"$HOME/Library/Application Support/Rack2/plugins-mac-arm64"}
for plugin_slug in Fundamental Valley; do
    test -d "$rack_library_plugins/$plugin_slug" || {
        printf 'Required E2E Rack plugin is not installed: %s/%s\n' "$rack_library_plugins" "$plugin_slug" >&2
        exit 1
    }
    cp -a "$rack_library_plugins/$plugin_slug" "$user_dir/plugins-mac-arm64/$plugin_slug"
done

# Preserve the preceding run but keep its completion markers from satisfying
# this run's waits. The latest evidence is repopulated at the canonical paths.
previous_dir="$runtime_dir/previous-evidence"
for artifact in first.done reload.done screenshot.done first-stop.request reload-stop.request results.raw.json reload-results.json results.json rack.log screenshot.png screenshot-provenance.json \
    default-first-sound.wav clock-120.wav clock-60.wav pause-resume-reset.wav one-shot.wav loop.wav \
    sections.wav multiple-instances.wav triggered-word.wav sustained-vowel-baseline.wav \
    sustained-vowel-modulated.wav sustained-vowel-shaped.wav sustained-vowel-reverb.wav \
    full-song-dry.wav full-song-reverb.wav reload-sound.wav saved-and-reloaded.vcv; do
    if test -f "$evidence_dir/$artifact"; then
        mkdir -p "$previous_dir"
        mv "$evidence_dir/$artifact" "$previous_dir/$artifact"
    fi
done

zstd -d -c "$production_package" | tar -xf - -C "$user_dir/plugins-mac-arm64"
zstd -d -c "$helper_package" | tar -xf - -C "$user_dir/plugins-mac-arm64"
cp tests/e2e/settings.json "$user_dir/settings.json"
python3 scripts/generate_e2e_patch.py "$patch_path" "$runtime_dir/external-test-bank"

wait_for_file() {
    target=$1
    count=0
    while test ! -f "$target"; do
        count=$((count + 1))
        test "$count" -lt 180 || { printf 'Timed out waiting for %s\n' "$target" >&2; return 1; }
        sleep 1
    done
}

stop_rack() {
    pid=$1
    kill -TERM "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

wait_for_exit() {
    pid=$1
    count=0
    while kill -0 "$pid" 2>/dev/null; do
        count=$((count + 1))
        test "$count" -lt 30 || return 1
        sleep 1
    done
    wait "$pid" 2>/dev/null || true
}

VOCALRACK_E2E_DIR="$evidence_dir" "$rack_bin" -u "$user_dir" "$patch_path" &
first_pid=$!
trap 'stop_rack "$first_pid"' INT TERM EXIT
wait_for_file "$evidence_dir/first.done"
if test "${VOCALRACK_E2E_EXTERNAL_SCREENSHOT:-0}" = 1; then
    printf 'Waiting for externally verified Rack screenshot evidence...\n'
    wait_for_file "$evidence_dir/screenshot.done"
    python3 -c 'import json,pathlib,sys; pathlib.Path(sys.argv[1]).write_text(json.dumps({"source":"external-verified","capturedApplication":"VCV Rack 2 Free"}, indent=2) + "\n")' \
        "$evidence_dir/screenshot-provenance.json"
elif command -v screencapture >/dev/null 2>&1; then
    frontmost_application=""
    if command -v osascript >/dev/null 2>&1; then
        osascript -e 'tell application id "com.vcvrack.rack2" to activate'
        sleep 1
        frontmost_application=$(osascript -e 'tell application "System Events" to get name of first application process whose frontmost is true')
        case "$frontmost_application" in
            Rack|"VCV Rack 2 Free") ;;
            *)
                printf 'Rack screenshot refused: frontmost application is %s\n' "$frontmost_application" >&2
                exit 1
                ;;
        esac
    fi
    screencapture -x "$evidence_dir/screenshot.png"
    python3 -c 'import json,pathlib,sys; pathlib.Path(sys.argv[1]).write_text(json.dumps({"source":"automated-frontmost","capturedApplication":sys.argv[2]}, indent=2) + "\n")' \
        "$evidence_dir/screenshot-provenance.json" "$frontmost_application"
fi
printf 'close\n' > "$evidence_dir/first-stop.request"
if ! wait_for_exit "$first_pid"; then
    printf 'Rack did not close gracefully after the first pass\n' >&2
    stop_rack "$first_pid"
    exit 1
fi
trap - INT TERM EXIT
cp "$user_dir/log.txt" "$runtime_dir/rack-first.log"

VOCALRACK_E2E_DIR="$evidence_dir" VOCALRACK_E2E_RELOAD=1 "$rack_bin" -u "$user_dir" "$evidence_dir/saved-and-reloaded.vcv" &
reload_pid=$!
trap 'stop_rack "$reload_pid"' INT TERM EXIT
wait_for_file "$evidence_dir/reload.done"
printf 'close\n' > "$evidence_dir/reload-stop.request"
if ! wait_for_exit "$reload_pid"; then
    printf 'Rack did not close gracefully after the reload pass\n' >&2
    stop_rack "$reload_pid"
    exit 1
fi
trap - INT TERM EXIT
cp "$user_dir/log.txt" "$runtime_dir/rack-reload.log"

{ printf '=== FIRST PASS ===\n'; cat "$runtime_dir/rack-first.log"; printf '\n=== RELOAD PASS ===\n'; cat "$runtime_dir/rack-reload.log"; } > "$evidence_dir/rack.log"
python3 scripts/validate_e2e.py "$evidence_dir"
if rg -i 'could not load plugin|assertion failed|segmentation fault|fatal error' "$evidence_dir/rack.log"; then
    printf 'Rack log contains a fatal/load error\n' >&2
    exit 1
fi
printf 'Real Rack E2E passed; evidence: %s\n' "$evidence_dir"
