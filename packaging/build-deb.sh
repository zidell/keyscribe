#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version="${KEYSCRIBE_VERSION:-0.1.2}"
if [[ ! "$version" =~ ^[0-9][0-9A-Za-z.+~-]*$ ]]; then echo 'Invalid Debian version' >&2; exit 1; fi
arch="$(dpkg --print-architecture)"
"$root/native/linux/build.sh"
output="${KEYSCRIBE_BUILD_DIR:-$root/dist-native/linux}"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
install -Dm755 "$output/keyscribe" "$stage/usr/bin/keyscribe"
install -Dm644 "$root/native/linux/net.gitools.keyscribe.desktop" "$stage/usr/share/applications/net.gitools.keyscribe.desktop"
for file in extension.js metadata.json; do
  install -Dm644 "$root/native/linux/gnome-extension/$file" "$stage/usr/share/gnome-shell/extensions/keyscribe-escape@gitools.net/$file"
done
install -Dm644 "$root/docs/readme.txt" "$stage/usr/share/doc/keyscribe/readme.txt"
install -Dm644 "$root/native/linux/README.md" "$stage/usr/share/doc/keyscribe/linux-readme.md"
install -Dm644 "$root/native/linux/vendor/tomlc17/LICENSE" "$stage/usr/share/doc/keyscribe/tomlc17-LICENSE"
install -Dm644 "$root/LICENSE" "$stage/usr/share/doc/keyscribe/copyright"
mkdir -p "$stage/DEBIAN"
cat > "$stage/DEBIAN/control" <<CONTROL
Package: keyscribe
Version: $version
Architecture: $arch
Maintainer: KeyScribe contributors <keyscribe@gitools.net>
Section: sound
Priority: optional
Depends: libgtk-3-0t64, libglib2.0-0t64 (>= 2.74), libayatana-appindicator3-1, libpulse0, libpulse-mainloop-glib0, libcurl4t64, libjson-glib-1.0-0
Recommends: xdg-desktop-portal, xdg-desktop-portal-gnome, xdg-desktop-portal-kde, gnome-shell-extension-appindicator, xwayland, iso-codes
Description: Native voice input for the Ubuntu desktop
 Records microphone audio, transcribes it with OpenAI, ElevenLabs or Groq,
 and pastes text into the active application using desktop portals.
CONTROL
dpkg-deb --root-owner-group --build "$stage" "$output/KeyScribe-ubuntu-$arch-$version.deb"
