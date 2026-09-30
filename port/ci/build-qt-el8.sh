#!/bin/bash
set -euo pipefail
. /etc/os-release
[[ "${VERSION_ID%%.*}" = 8 && "${ID_LIKE:-$ID}" = *rhel* || "$ID" = rhel && "${VERSION_ID%%.*}" = 8 ]] || {
    echo 'The release Qt SDK must be built on EL8.' >&2; exit 2;
}
version=5.15.18
sha=cea1fbabf02455f3f0e8eaa839f5d6f45cdb56b62c8a83af5c1d00ac05f912ea
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$root/build/qt
mkdir -p "$work"
archive=$work/qt-everywhere-opensource-src-$version.tar.xz
if [ ! -f "$archive" ]; then
    curl --fail --location --retry 3 -o "$archive" \
        "https://download.qt.io/archive/qt/5.15/$version/single/qt-everywhere-opensource-src-$version.tar.xz"
fi
printf '%s  %s\n' "$sha" "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"
source_dir=$work/qt-everywhere-src-$version
[ -x "$source_dir/configure" ] || { echo 'unexpected Qt archive layout' >&2; exit 1; }
mkdir -p "$work/obj"
cd "$work/obj"
skip=()
for module in "$source_dir"/qt*; do
    [ -d "$module" ] || continue
    name=${module##*/}
    case "$name" in qtbase|qtcharts|qtconnectivity|qtimageformats|qtsvg|qtwayland) continue;; esac
    skip+=(-skip "$name")
done
"$source_dir/configure" -prefix /opt/octool-qt -release -static \
    -opensource -confirm-license -nomake examples -nomake tests \
    -no-icu -qt-libjpeg -qt-libpng -qt-zlib -qt-pcre -qt-harfbuzz \
    -qt-tiff -qt-webp \
    -fontconfig -system-freetype -accessibility -feature-accessibility-atspi-bridge \
    -dbus-linked -xcb -xcb-xlib \
    -opengl desktop "${skip[@]}"
# Top-level and standalone QtBase builds can put the summary in different
# directories. Select an actual generated file, never infer it from a script's
# working-directory change (top-level qmake can change OUT_PWD again).
summary=
for candidate in config.summary qtbase/config.summary; do
    if [ -s "$candidate" ]; then summary=$candidate; break; fi
done
if [ -z "$summary" ]; then
    echo 'Qt configure produced no config.summary in the supported build layouts' >&2
    find . -maxdepth 3 -name config.summary -print >&2
    exit 1
fi
printf 'Qt configuration summary: %s\n' "$summary"
make -j"${JOBS:-2}"
make install
grep -Eq '^#define QT_FEATURE_accessibility 1$' /opt/octool-qt/include/QtGui/qtgui-config.h
# AT-SPI is a private Qt feature; its header is in the versioned private tree.
grep -REq '^#define QT_FEATURE_accessibility_atspi_bridge 1$' /opt/octool-qt/include/QtGui
test -f /opt/octool-qt/plugins/platforms/libqxcb.a
python3 - "$source_dir" <<'PY'
import pathlib, shutil, sys
src = pathlib.Path(sys.argv[1])
dst = pathlib.Path('/opt/octool-qt/licenses')
for p in src.rglob('*'):
    if p.is_file() and (p.name.startswith('LICENSE') or p.name == 'qt_attribution.json'):
        target = dst / p.relative_to(src)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
PY
printf 'Qt %s\nsource sha256 %s\n' "$version" "$sha" > /opt/octool-qt/licenses/build-source.txt
cp "$summary" /opt/octool-qt/licenses/config.summary
