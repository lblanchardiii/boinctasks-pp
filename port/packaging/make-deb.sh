#!/bin/bash
# Build a .deb that depends on the distro's wxWidgets / SQLite packages.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
# BUILD can point at a container build; defaults to the local one
BUILD="${BUILD:-$ROOT/port/build}"
VERSION="${VERSION:-0.9.0}"
PKG="$HERE/debroot"

[ -x "$BUILD/boinctasks" ] || { echo "build first: make -C port/build"; exit 1; }

rm -rf "$PKG"
mkdir -p "$PKG/DEBIAN" "$PKG/usr/bin" "$PKG/usr/share/applications" \
         "$PKG/usr/share/doc/boinctasks-pp" \
         "$PKG/usr/share/icons/hicolor/256x256/apps"

install -m 755 "$BUILD/boinctasks" "$PKG/usr/bin/boinctasks-pp"
strip "$PKG/usr/bin/boinctasks-pp" 2>/dev/null || true

# resolve runtime deps from the actual libraries the binary links
DEPS=$(dpkg-shlibdeps -O --ignore-missing-info "$BUILD/boinctasks" 2>/dev/null \
        | sed 's/^shlibs:Depends=//')
# Fallback for the release build: wxWidgets is linked statically there, so the
# package must NOT depend on a distro wx - 22.04 has no wx3.2 package at all.
[ -n "$DEPS" ] || DEPS="libc6 (>= 2.34), libgtk-3-0, libsqlite3-0, libexpat1, libsm6, libnotify4"

cat > "$PKG/DEBIAN/control" <<CONTROL
Package: boinctasks-pp
Version: $VERSION
Section: net
Priority: optional
Architecture: amd64
Depends: $DEPS
Suggests: boinc-client
Maintainer: BoincTasks Linux port <noreply@example.com>
Description: BoincTasks++ - manage BOINC clients across many computers
 A native Linux port of eFMer BoincTasks: monitor and control large numbers
 of BOINC clients from one window, with grouped task lists, per-computer
 filtering, persistent history and remote operations.
CONTROL

cat > "$PKG/usr/share/applications/boinctasks-pp.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=BoincTasks++
Comment=Manage BOINC clients across many computers
Exec=boinctasks-pp
Icon=boinctasks-pp
Categories=Network;Monitor;
Terminal=false
DESKTOP

cp "$HERE/win/icon-256.png" \
   "$PKG/usr/share/icons/hicolor/256x256/apps/boinctasks-pp.png" 2>/dev/null || true
cp "$ROOT/LICENSE.txt" "$PKG/usr/share/doc/boinctasks-pp/copyright" 2>/dev/null || true

dpkg-deb --build --root-owner-group "$PKG" \
         "$ROOT/boinctasks-pp_${VERSION}_amd64.deb"
