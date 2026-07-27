#!/bin/bash
# Release packaging, run inside bt-build22. Everything - binary, bundled
# libraries and dependency resolution - comes from the 22.04 image, so nothing
# from a newer host can raise the glibc floor.
set -e
SRC=/src
export BUILD=/tmp/build22
export VERSION="${VERSION:-0.9.0}"

bash "$SRC/port/packaging/build-in-container.sh"

export PATH=/opt/appimagetool:$PATH
export APPIMAGE_EXTRACT_AND_RUN=1
export BUNDLE_LIBS=0        # static wx: nothing left worth carrying
cd "$SRC/port/packaging"
./make-appimage.sh
./make-deb.sh

mkdir -p "$SRC/release-22.04"
mv -f "$SRC"/BoincTasksPP-*.AppImage "$SRC/release-22.04/" 2>/dev/null || true
mv -f "$SRC"/boinctasks-pp_*.deb "$SRC/release-22.04/" 2>/dev/null || true
ls -lh "$SRC/release-22.04/"
