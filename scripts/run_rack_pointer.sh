#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
driver="$repo_dir/test-artifacts/user-journey/bin/macos-pointer-click"
test -x "$driver" || {
    printf 'Pointer driver is not built: %s\n' "$driver" >&2
    exit 1
}

# Keep activation and the HID event in one process invocation. If they are
# split across automation calls, the Codex window can become frontmost again
# between them and receive the click intended for the floating Rack window.
# Address the application by its installed display name. macOS sometimes fails
# to resolve an application-id target while a native open panel is being torn
# down, even though the already-running Rack process is still addressable by
# name.
osascript -e 'tell application "VCV Rack 2 Free" to activate'
sleep 0.35
exec "$driver" "$@"
