#!/bin/sh
set -eu

dist_dir=${1:-dist}
test -d "$dist_dir" || {
    printf 'release directory not found: %s\n' "$dist_dir" >&2
    exit 1
}

set -- "$dist_dir"/*.vcvplugin
test -f "$1" || {
    printf 'no .vcvplugin archives found in %s\n' "$dist_dir" >&2
    exit 1
}

manifest="$dist_dir/SHA256SUMS"
if command -v sha256sum >/dev/null 2>&1; then
    (cd "$dist_dir" && sha256sum ./*.vcvplugin) > "$manifest"
elif command -v shasum >/dev/null 2>&1; then
    (cd "$dist_dir" && shasum -a 256 ./*.vcvplugin) > "$manifest"
else
    printf 'neither sha256sum nor shasum is available\n' >&2
    exit 1
fi

printf 'wrote %s\n' "$manifest"
