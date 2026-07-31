#!/bin/bash
# Release build, run inside the bt-build22 container (Ubuntu 22.04 / glibc 2.35).
#
#   podman run --rm -v /root/projects/boinctasks-linux:/src bt-build22 \
#       /src/port/packaging/build-in-container.sh
#
# Produces a binary that runs on 22.04 and 24.04 LTS, Debian 12/13 and current
# Fedora/openSUSE. wxWidgets is linked statically from /opt/wx32, so the only
# runtime dependencies are GTK, SQLite and the C/C++ runtimes.
set -e

SRC=/src
OUT=$SRC/release-22.04
BUILD=/tmp/build22          # keep object files out of the mounted tree

echo "== configuring =="
# Pass the version through, or the binary reports whatever the CMakeLists
# default happens to be while the package filename says something else. That
# stayed hidden only because both were bumped by hand at the same time.
cmake -S "$SRC/port" -B "$BUILD" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBT_VERSION="${VERSION:?VERSION must be set}" \
      -DwxWidgets_CONFIG_EXECUTABLE=/opt/wx32/bin/wx-config

echo "== building =="
make -C "$BUILD" -j"$(nproc)" boinctasks

mkdir -p "$OUT"
cp "$BUILD/boinctasks" "$OUT/boinctasks"
strip "$OUT/boinctasks"

echo "== symbol floor =="
echo -n "glibc:   "; objdump -T "$OUT/boinctasks" | grep -oE "GLIBC_[0-9.]+" | sort -uV | tail -1
echo -n "glibcxx: "; objdump -T "$OUT/boinctasks" | grep -oE "GLIBCXX_[0-9.]+" | sort -uV | tail -1
echo "shared libraries still needed:"
ldd "$OUT/boinctasks" | awk '{print "  " $1}' | sort | head -30
