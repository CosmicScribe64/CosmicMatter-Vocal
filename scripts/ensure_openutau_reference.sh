#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
reference_dir="$repo_dir/reference/OpenUtau"
expected=$(tr -d '[:space:]' < "$repo_dir/reference/OPENUTAU_COMMIT.txt")
source_url=${OPENUTAU_SOURCE_URL:-https://github.com/stakira/OpenUtau.git}

if test -d "$reference_dir/.git"; then
    actual=$(git -C "$reference_dir" rev-parse HEAD)
    if test "$actual" = "$expected"; then
        exit 0
    fi
    printf 'OpenUtau reference is at %s, expected %s; move it aside before bootstrapping\n' "$actual" "$expected" >&2
    exit 1
fi

if test -e "$reference_dir"; then
    printf '%s exists but is not a Git checkout; move it aside before bootstrapping\n' "$reference_dir" >&2
    exit 1
fi

git clone --filter=blob:none --no-checkout "$source_url" "$reference_dir"
git -C "$reference_dir" checkout --detach "$expected"
actual=$(git -C "$reference_dir" rev-parse HEAD)
test "$actual" = "$expected" || { printf 'OpenUtau checkout verification failed\n' >&2; exit 1; }
