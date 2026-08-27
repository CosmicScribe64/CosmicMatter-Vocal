#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image_name=vocalrack-core-tests:ubuntu-24.04

docker build --tag "$image_name" --file "$repo_dir/Dockerfile" "$repo_dir"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "$repo_dir:/workspace" \
    --workdir /workspace \
    "$image_name" \
    make -f tests/Makefile BUILD=build-tests-docker tests offline
