#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
output="${KEYSCRIBE_BUILD_DIR:-$root/dist-native/linux}"
if pgrep -x keyscribe >/dev/null; then
  echo 'Quit idle KeyScribe before the private KDE Escape test.' >&2
  exit 1
fi
python3 "$root/native/linux/kwin-extension/build.py" --test-driver
install -Dm755 "$output/keyscribe-escape-driver.so" "$HOME/.local/lib/qt6/plugins/kwin/effects/plugins/keyscribe-escape-driver.so"
trap 'busctl --user call org.kde.KWin /Effects org.kde.kwin.Effects unloadEffect s keyscribe-escape-driver >/dev/null; rm -f "$HOME/.local/lib/qt6/plugins/kwin/effects/plugins/keyscribe-escape-driver.so"' EXIT
busctl --user call org.kde.KWin /Effects org.kde.kwin.Effects loadEffect s keyscribe-escape-driver
"${CC:-cc}" -std=c11 -Wall -Wextra -DKEYSCRIBE_KDE_ESCAPE_TEST $(pkg-config --cflags gtk+-3.0 gio-unix-2.0) -I"$root/native/linux/src" "$root/native/linux/tests/escape_smoke.c" "$root/native/linux/src/portal.c" -o "$output/kwin-escape-smoke" $(pkg-config --libs gtk+-3.0 gio-unix-2.0)
GDK_BACKEND=wayland timeout 20s "$output/kwin-escape-smoke"
