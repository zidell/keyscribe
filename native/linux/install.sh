#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
binary="${KEYSCRIBE_BUILD_DIR:-$root/dist-native/linux}/keyscribe"
if [[ ! -x "$binary" ]]; then "$root/native/linux/build.sh"; fi
prefix="${KEYSCRIBE_INSTALL_PREFIX:-$HOME/.local}"
install -Dm755 "$binary" "$prefix/bin/keyscribe"
install -Dm644 "$root/docs/readme.txt" "$prefix/share/doc/keyscribe/readme.txt"
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
printf 'Installed %s/bin/keyscribe\n' "$prefix"
