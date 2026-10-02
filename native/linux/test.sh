#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
output="${KEYSCRIBE_BUILD_DIR:-$root/dist-native/linux}"
mkdir -p "$output"
settings_sources=("$root/native/linux/src/settings.c" "$root/native/linux/vendor/tomlc17/tomlc17.c")
if [[ "${1:-}" == --escape ]]; then
  exec "$root/native/linux/tests/escape_test.sh"
fi
if [[ "${1:-}" == --input ]]; then
  read -r -a input_cflags <<< "$(pkg-config --cflags gtk+-3.0 gio-unix-2.0)"
  read -r -a input_libs <<< "$(pkg-config --libs gtk+-3.0 gio-unix-2.0)"
  "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic ${CFLAGS:-} "${input_cflags[@]}" \
    -I"$root/native/linux/src" "$root/native/linux/tests/input_smoke.c" \
    "$root/native/linux/src/portal.c" -o "$output/input-smoke" ${LDFLAGS:-} "${input_libs[@]}"
  exec "$output/input-smoke"
fi
read -r -a cflags <<< "$(pkg-config --cflags gio-2.0 libcurl json-glib-1.0)"
read -r -a libs <<< "$(pkg-config --libs gio-2.0 libcurl json-glib-1.0)"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic ${CFLAGS:-} "${cflags[@]}" -I"$root/native/linux/src" \
  "$root/native/linux/tests/core_test.c" "$root/native/linux/src/core.c" "${settings_sources[@]}" -o "$output/core-test" ${LDFLAGS:-} "${libs[@]}"
"$output/core-test"

"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic ${CFLAGS:-} "${cflags[@]}" -I"$root/native/linux/src" \
  '-DKEYSCRIBE_TEST_ENDPOINT=g_getenv("KEYSCRIBE_TEST_ENDPOINT")' \
  "$root/native/linux/tests/transcribe_fixture.c" "$root/native/linux/src/core.c" "${settings_sources[@]}" -o "$output/transcribe-fixture" ${LDFLAGS:-} "${libs[@]}"
python3 "$root/native/linux/tests/http_test.py" "$output/transcribe-fixture"
if [[ "${1:-}" == --desktop ]]; then
  read -r -a desktop_cflags <<< "$(pkg-config --cflags gtk+-3.0 ayatana-appindicator3-0.1 libpulse-mainloop-glib libpulse-simple libcurl json-glib-1.0 gio-unix-2.0)"
  read -r -a desktop_libs <<< "$(pkg-config --libs gtk+-3.0 ayatana-appindicator3-0.1 libpulse-mainloop-glib libpulse-simple libcurl json-glib-1.0 gio-unix-2.0)"
  python3 "$root/native/linux/embed_sounds.py" "$output/sounds.h"
  "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic ${CFLAGS:-} "${desktop_cflags[@]}" -I"$output" \
    "$root/native/linux/tests/desktop_smoke.c" "$root/native/linux/src/core.c" "${settings_sources[@]}" "$root/native/linux/src/portal.c" "$root/native/linux/src/output.c" "$root/native/linux/src/overlay.c" \
    -o "$output/desktop-smoke" ${LDFLAGS:-} "${desktop_libs[@]}" -lm
  "$output/desktop-smoke"
fi
read -r -a portal_cflags <<< "$(pkg-config --cflags gio-unix-2.0)"
read -r -a portal_libs <<< "$(pkg-config --libs gio-unix-2.0)"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic ${CFLAGS:-} "${portal_cflags[@]}" -I"$root/native/linux/src" \
  "$root/native/linux/tests/portal_test.c" "$root/native/linux/src/portal.c" -o "$output/portal-test" ${LDFLAGS:-} "${portal_libs[@]}"
"$output/portal-test"

"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic ${CFLAGS:-} "${portal_cflags[@]}" \
  "$root/native/linux/tests/ibus_shortcut_test.c" -o "$output/ibus-shortcut-test" ${LDFLAGS:-} "${portal_libs[@]}"
"$output/ibus-shortcut-test"

read -r -a overlay_cflags <<< "$(pkg-config --cflags gtk+-3.0)"
read -r -a overlay_libs <<< "$(pkg-config --libs gtk+-3.0)"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic ${CFLAGS:-} "${overlay_cflags[@]}" \
  "$root/native/linux/tests/overlay_test.c" -o "$output/overlay-test" ${LDFLAGS:-} "${overlay_libs[@]}" -lm
"$output/overlay-test" "$output/widget-preview.png"
