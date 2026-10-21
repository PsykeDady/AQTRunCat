#!/usr/bin/env bash
# The package recipes must carry the project version from CMakeLists.txt.
# Usage: check_versions.sh SOURCE_DIR
set -euo pipefail
root=$1
cmake=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -n1)
arch=$(sed -n 's/^pkgver=//p' "$root/packaging/arch/PKGBUILD")
rpm=$(sed -n 's/^Version: *//p' "$root/packaging/rpm/aqtruncat.spec")
deb=$(sed -n '1s/^aqtruncat (\([^)-]*\)-[^)]*).*/\1/p' "$root/packaging/debian/changelog")
man=$(sed -n 's/^\.TH AQTRUNCAT 1 "[^"]*" "AQTRunCat \([0-9.]*\)".*/\1/p' "$root/data/aqtruncat.1")
plasmoid=$(sed -n 's/^ *"Version": *"\([0-9.]*\)".*/\1/p' "$root/plasmoid/package/metadata.json")
status=0
for pair in "PKGBUILD:$arch" "aqtruncat.spec:$rpm" "debian/changelog:$deb" "aqtruncat.1:$man" "plasmoid metadata.json:$plasmoid"; do
    if [[ ${pair#*:} != "$cmake" ]]; then
        echo "FAIL: ${pair%%:*} has version '${pair#*:}', CMakeLists.txt has '$cmake'"
        status=1
    fi
done
((status == 0)) && echo "package versions match $cmake"
exit $status
