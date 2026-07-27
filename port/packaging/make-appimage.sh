#!/bin/bash
# Build a self-contained AppImage: one file, chmod +x, run. No install.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
# BUILD can point at a container build; defaults to the local one
BUILD="${BUILD:-$ROOT/port/build}"
APPDIR="$HERE/AppDir"
VERSION="${VERSION:-0.9.0}"

[ -x "$BUILD/boinctasks" ] || { echo "build first: make -C port/build"; exit 1; }

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

install -m 755 "$BUILD/boinctasks" "$APPDIR/usr/bin/boinctasks"

# Bundle the shared libraries the binary needs, minus the ones every glibc
# system already provides (bundling those breaks on other distros).
# Anything that talks to the host's display server or kernel drivers must come
# from the host, not the bundle - a bundled libXext against the host's X server
# is how AppImages break on other distros. libX11 alone missed the whole libX*
# family (libXext, libXi, libXrandr, libXcursor, libXfixes, ...), so match the
# family and the rest of the graphics/session stack.
EXCLUDE='^(libc|libm|libdl|libpthread|librt|libstdc\+\+|libgcc_s|ld-linux|libX[0-9A-Za-z]*|libxcb|libxkbcommon|libSM|libICE|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libwayland|libdrm|libgbm|libudev|libdbus)'
# With wxWidgets linked statically there is nothing left worth bundling: what
# remains is GTK, GLib, Cairo, SQLite and friends, all present on any desktop
# and all safer taken from the host, where they match its themes, input methods
# and pixbuf loaders. BUNDLE_LIBS=0 skips the copy entirely.
# Even with nothing else bundled, carry the few libraries a desktop might not
# have: libnotify is not universal, and libSM/libICE are missing from minimal
# installs. All three are small, stable and not tied to the display driver.
ALWAYS_BUNDLE='^(libnotify\.so|libSM\.so|libICE\.so)'
ldd "$BUILD/boinctasks" | awk '/=> \//{print $3}' | sort -u | while read -r lib; do
    if [ "${BUNDLE_LIBS:-1}" = "0" ]; then
        basename "$lib" | grep -Eq "$ALWAYS_BUNDLE" || continue
        cp -L "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
        continue
    fi
    base=$(basename "$lib")
    echo "$base" | grep -Eq "$EXCLUDE" && continue
    cp -L "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
done

cat > "$APPDIR/boinctasks.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=BoincTasks++
Comment=Manage BOINC clients across many computers
Exec=boinctasks
Icon=boinctasks
Categories=Network;Monitor;
Terminal=false
DESKTOP
cp "$APPDIR/boinctasks.desktop" "$APPDIR/usr/share/applications/"

# minimal icon so the AppImage tooling is satisfied
python3 - "$APPDIR/boinctasks.png" <<'PY'
import struct, zlib, sys
w = h = 256
rows = b''
for y in range(h):
    row = b'\x00'
    for x in range(w):
        band = y * 4 // h
        rgb = [(31,119,180),(44,160,44),(255,150,60),(214,39,40)][min(band,3)]
        row += bytes(rgb)
    rows += row
def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(rows, 9))
png += chunk(b'IEND', b'')
open(sys.argv[1], 'wb').write(png)
PY
cp "$APPDIR/boinctasks.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/"

cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
exec "$HERE/usr/bin/boinctasks" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# appimagetool if available, else a self-extracting fallback archive
if command -v appimagetool-real >/dev/null 2>&1; then
    ARCH=x86_64 appimagetool-real "$APPDIR" "$ROOT/BoincTasksPP-$VERSION-x86_64.AppImage"
else
    echo "appimagetool not found - building a portable tar instead"
    tar -C "$HERE" -czf "$ROOT/BoincTasksPP-$VERSION-x86_64-portable.tar.gz" AppDir
    echo "  extract and run AppDir/AppRun"
fi
