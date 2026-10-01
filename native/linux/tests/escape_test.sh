#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
output="${KEYSCRIBE_BUILD_DIR:-$root/dist-native/linux}"
if [[ "${1:-}" != --isolated ]]; then
  command -v gnome-shell >/dev/null
  read -r -a cflags <<< "$(pkg-config --cflags gtk+-3.0 gio-unix-2.0)"
  read -r -a libs <<< "$(pkg-config --libs gtk+-3.0 gio-unix-2.0)"
  mkdir -p "$output"
  "${CC:-cc}" -std=c11 -Wall -Wextra ${CFLAGS:-} "${cflags[@]}" -I"$root/native/linux/src" \
    "$root/native/linux/tests/escape_smoke.c" "$root/native/linux/src/portal.c" \
    -o "$output/escape-smoke" ${LDFLAGS:-} "${libs[@]}"
  sandbox="$(mktemp -d)"
  trap 'rm -rf "$sandbox"' EXIT
  export KEYSCRIBE_ESCAPE_SANDBOX="$sandbox"
  export KEYSCRIBE_BUILD_DIR="$output"
  if ! timeout 30s dbus-run-session "$0" --isolated >"$sandbox/test.log" 2>&1; then
    cat "$sandbox/test.log" "$sandbox/shell.log"
    exit 1
  fi
  if ! rg 'PASS:' "$sandbox/test.log"; then
    cat "$sandbox/test.log" "$sandbox/shell.log"
    exit 1
  fi
  exit
fi
export XDG_RUNTIME_DIR="$KEYSCRIBE_ESCAPE_SANDBOX/runtime"
export XDG_CONFIG_HOME="$KEYSCRIBE_ESCAPE_SANDBOX/config"
export XDG_DATA_HOME="$KEYSCRIBE_ESCAPE_SANDBOX/data"
export GSETTINGS_BACKEND=keyfile
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=GNOME
export WAYLAND_DISPLAY=keyscribe-escape-test
extension="$XDG_DATA_HOME/gnome-shell/extensions/keyscribe-escape@gitools.net"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$extension"
chmod 700 "$XDG_RUNTIME_DIR"
cp "$root/native/linux/gnome-extension/"{extension.js,metadata.json} "$extension/"
cp "$root/native/linux/tests/escape_driver.js" "$extension/driver.js"
printf '\nimport "./driver.js";\n' >> "$extension/extension.js"
gsettings set org.gnome.desktop.wm.keybindings cycle-panels "['<Control><Alt>Escape']"
gsettings set org.gnome.desktop.wm.keybindings cycle-windows "['<Alt>Escape']"
gsettings set org.gnome.shell disable-user-extensions false
gsettings set org.gnome.shell enabled-extensions "['keyscribe-escape@gitools.net']"
env -u LD_LIBRARY_PATH -u GI_TYPELIB_PATH gnome-shell --headless --wayland \
  --virtual-monitor 800x600 --wayland-display "$WAYLAND_DISPLAY" \
  >"$KEYSCRIBE_ESCAPE_SANDBOX/shell.log" 2>&1 &
shell_pid=$!
trap 'kill "$shell_pid" 2>/dev/null || true' EXIT
for _ in {1..60}; do
  if gdbus introspect --session --dest net.gitools.keyscribe.Driver \
    --object-path /net/gitools/keyscribe/Driver >/dev/null 2>&1; then break; fi
  sleep .25
done
GDK_BACKEND=wayland "$output/escape-smoke"
