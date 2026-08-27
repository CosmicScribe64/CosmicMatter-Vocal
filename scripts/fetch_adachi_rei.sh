#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
destination="$repo_dir/development-assets/adachi-rei-ver3.5.0.zip"
url='https://www.dropbox.com/scl/fi/8z9corxo32gwgqyv5z92a/ver3.5.0.zip?rlkey=53n6e60cozvp3c9nvwyz6odxx&dl=1'
expected='b96d1b21145f22e573afd9ec8aeaad0ec9cbaee581c2623c64addeb31de46b3d'

mkdir -p "$repo_dir/development-assets"
curl --fail --location --output "$destination" "$url"
actual=$(shasum -a 256 "$destination" | awk '{print $1}')
test "$actual" = "$expected" || { printf 'Adachi Rei archive hash mismatch: %s\n' "$actual" >&2; exit 1; }
printf 'Downloaded verified Adachi Rei 3.5.0 archive to %s\n' "$destination"
