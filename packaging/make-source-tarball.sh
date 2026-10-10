#!/usr/bin/env bash
# Create aqtruncat-<version>.tar.gz (top directory aqtruncat-<version>/) from
# this source tree, without build trees or local artefacts. The version comes
# from project(... VERSION ...) in CMakeLists.txt.
# Usage: packaging/make-source-tarball.sh [OUTPUT_DIR]   (default: current dir)

set -Eeuo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=$(mkdir -p -- "${1:-.}" && cd -- "${1:-.}" && pwd)
version=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -n1)
[[ -n $version ]] || { echo "make-source-tarball.sh: cannot read the version from CMakeLists.txt" >&2; exit 1; }
name=aqtruncat-$version

tar --create --gzip --file "$out/$name.tar.gz" \
    --directory "$root" \
    --transform "s|^\\.|$name|" \
    --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@${SOURCE_DATE_EPOCH:-$(stat -c %Y "$root/CMakeLists.txt")}" \
    --exclude=./build --exclude='./build-*' --exclude='./cmake-build-*' \
    --exclude=./.git --exclude=./.cache --exclude=./.idea --exclude=./.vscode \
    --exclude='CMakeLists.txt.user*' --exclude=CMakeUserPresets.json \
    --exclude=__pycache__ --exclude='*.pyc' \
    --exclude='./docs/M1-tray-spike/*.png' \
    --exclude=./packaging/arch/src --exclude=./packaging/arch/pkg \
    --exclude='./packaging/arch/*.tar.*' \
    .
echo "$out/$name.tar.gz"
