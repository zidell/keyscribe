#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
binary="${KEYSCRIBE_BUILD_DIR:-$root/dist-native/linux}/keyscribe"
if [[ ! -x "$binary" ]]; then "$root/native/linux/build.sh"; fi
prefix="${KEYSCRIBE_INSTALL_PREFIX:-$HOME/.local}"
install -Dm755 "$binary" "$prefix/bin/keyscribe"
install -Dm644 "$root/docs/readme.txt" "$prefix/share/doc/keyscribe/readme.txt"
install -Dm644 "$root/native/linux/README.md" "$prefix/share/doc/keyscribe/linux-readme.md"
install -Dm644 "$root/native/linux/vendor/tomlc17/LICENSE" "$prefix/share/doc/keyscribe/tomlc17-LICENSE"
mkdir -p "$prefix/share/applications"
python3 - "$prefix" "$root/native/linux/net.gitools.keyscribe.desktop" <<'PY'
import pathlib,sys
prefix=pathlib.Path(sys.argv[1]).resolve()
# Desktop entry Exec uses its own quoting/escaping rules, not shell quoting.
exe=str(prefix/'bin/keyscribe').replace('\\','\\\\\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')
text=pathlib.Path(sys.argv[2]).read_text().replace('Exec=keyscribe',f'Exec="{exe}"')
(prefix/'share/applications/net.gitools.keyscribe.desktop').write_text(text)
PY
if command -v update-desktop-database >/dev/null; then update-desktop-database "$prefix/share/applications"; fi
extension=keyscribe-escape@gitools.net
for file in extension.js metadata.json; do
  install -Dm644 "$root/native/linux/gnome-extension/$file" "$prefix/share/gnome-shell/extensions/$extension/$file"
done
if [[ "${XDG_CURRENT_DESKTOP:-}" == *GNOME* ]] && command -v gsettings >/dev/null; then
  python3 - "$extension" <<'PYTHON'
import sys
from gi.repository import Gio
settings = Gio.Settings.new('org.gnome.shell')
name = sys.argv[1]
enabled = settings.get_strv('enabled-extensions')
if name not in enabled:
    settings.set_strv('enabled-extensions', enabled + [name])
disabled = settings.get_strv('disabled-extensions')
if name in disabled:
    settings.set_strv('disabled-extensions', [item for item in disabled if item != name])
Gio.Settings.sync()
PYTHON
  echo 'GNOME: log out and back in once to load the new Escape cancellation extension.'
fi
kwin_extension="$(dirname "$binary")/keyscribe-escape.so"
if [[ -f "$kwin_extension" ]]; then
  install -Dm755 "$kwin_extension" "$prefix/lib/qt6/plugins/kwin/effects/plugins/keyscribe-escape.so"
  bootstrap="$prefix/share/kwin/scripts/keyscribe-escape-bootstrap"
  install -Dm644 "$root/native/linux/kwin-extension/bootstrap-metadata.json" "$bootstrap/metadata.json"
  install -Dm644 "$root/native/linux/kwin-extension/bootstrap.qml" "$bootstrap/contents/ui/main.qml"
  install -Dm755 "$(dirname "$binary")/libkeyscribebootstrap.so" "$bootstrap/contents/ui/bootstrap/libkeyscribebootstrap.so"
  printf 'module KeyScribeBootstrap\nplugin keyscribebootstrap\n' > "$bootstrap/contents/ui/bootstrap/qmldir"
  if command -v kwriteconfig6 >/dev/null; then
    kwriteconfig6 --file kwinrc --group Plugins --key keyscribe-escape-bootstrapEnabled true
  fi
fi
# Both backends can coexist; the desktop selects its own at login.
if command -v dpkg-query >/dev/null; then
  for backend in xdg-desktop-portal-gnome xdg-desktop-portal-kde; do
    if [[ "$(dpkg-query -W -f='${Status}' "$backend" 2>/dev/null || true)" != 'install ok installed' ]]; then
      printf 'Missing desktop backend: %s (install with sudo apt install %s)\n' "$backend" "$backend" >&2
    fi
  done
fi
printf 'Installed %s/bin/keyscribe\n' "$prefix"
