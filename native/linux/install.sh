#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
binary="${KEYSCRIBE_BUILD_DIR:-$root/dist-native/linux}/keyscribe"
if [[ ! -x "$binary" ]]; then "$root/native/linux/build.sh"; fi
prefix="${KEYSCRIBE_INSTALL_PREFIX:-$HOME/.local}"
install -Dm755 "$binary" "$prefix/bin/keyscribe"
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
printf 'Installed %s/bin/keyscribe\n' "$prefix"
