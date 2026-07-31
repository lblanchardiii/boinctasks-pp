#!/bin/bash
# Windows build, run inside the bt-win64 container:
#
#   podman run --rm -v /root/projects/boinctasks-pp:/src:z bt-win64 \
#       bash /src/port/packaging/build-windows.sh
#
# Produces a single static BoincTasksPP.exe - no MSVC redistributable, no
# wxWidgets or MinGW DLLs to ship alongside it.
set -e

SRC=/src
OUT=$SRC/release-windows
BUILD=/tmp/build-win

echo "== configuring =="
cmake -S "$SRC/port" -B "$BUILD" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
      -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
      -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
      -DCMAKE_FIND_ROOT_PATH="/opt/wx32-win;/usr/x86_64-w64-mingw32" \
      -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
      -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
      -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
      -DwxWidgets_CONFIG_EXECUTABLE=/opt/wx32-win/bin/wx-config \
      -DBT_SQLITE_LIB=/opt/libsqlite3.a \
      -DBT_SQLITE_INCLUDE=/opt/sqlite-include \
      -DBT_VERSION="${VERSION:?VERSION must be set}"

echo "== building =="
make -C "$BUILD" -j"$(nproc)" boinctasks

mkdir -p "$OUT"
cp "$BUILD/BoincTasksPP.exe" "$OUT/"
x86_64-w64-mingw32-strip "$OUT/BoincTasksPP.exe"

echo "== installer =="
if command -v makensis >/dev/null 2>&1; then
    # A test build carries a letter (0.9.4b). NSIS takes four integers and
    # nothing else, so the letter becomes the fourth field: 0.9.4.2. Releases
    # have no letter and keep 0.
    VER="${VERSION:-0.9.4}"
    VBASE="${VER%[a-z]}"
    VLETTER="${VER#"$VBASE"}"
    VBUILD=0
    if [ -n "$VLETTER" ]; then
        VBUILD=$(( $(printf '%d' "'$VLETTER") - 96 ))
    fi
    VERNUM="$VBASE.$VBUILD"
    echo "  version $VER  ->  numeric $VERNUM"

    cp "$SRC/LICENSE.txt" "$SRC/port/packaging/win/license.txt" 2>/dev/null || true
    makensis -V2 -DVERSION="$VER" -DVERSION_NUM="$VERNUM" \
             -DSRCEXE="$OUT/BoincTasksPP.exe" \
             -DOUTFILE="$OUT/BoincTasksPP-${VER}-setup.exe" \
             "$SRC/port/packaging/win/installer.nsi"
    ls -lh "$OUT"/*setup.exe | awk '{print "  " $5, $9}'
else
    echo "  makensis not present - skipping the installer"
fi

echo "== result =="
file "$OUT/BoincTasksPP.exe"
ls -lh "$OUT/BoincTasksPP.exe" | awk '{print "  size: " $5}'
echo "  DLLs it still needs (system ones only is what we want):"
x86_64-w64-mingw32-objdump -p "$OUT/BoincTasksPP.exe" \
    | awk '/DLL Name:/{print "    " $3}' | sort -u
