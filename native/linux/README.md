# KeyScribe for Ubuntu (GNOME and KDE Plasma)

Ubuntu support is an additional native implementation alongside `native/macos`
and `native/windows`. This app is compiled C with GTK 3, Ayatana AppIndicator,
PulseAudio (including PipeWire's PulseAudio server), libcurl, and JSON-GLib.
There is no webview or bundled browser runtime.

## Build and install

Target: Ubuntu 24.04 or newer. Build packages on the oldest Ubuntu release you
intend to distribute them to. A binary built on a newer system may require newer
libraries. Ubuntu 26.04 GNOME/Wayland is the local development environment.

Download the Ubuntu amd64 `.deb` and its SHA-256 checksum from
[the latest Ubuntu release](https://github.com/zidell/keyscribe/releases?q=linux-v&expanded=true).
Open the downloaded package to install, or run
`sudo apt install ./KeyScribe-ubuntu-amd64-<version>.deb`.
The installed app checks the landing page's `linux-version.txt` every six hours and adds a
**새 버전 … 다운로드** item to the tray menu when a newer release is out; install that
package the same way to update.
To build from source:

```sh
sudo apt install build-essential pkg-config libgtk-3-dev \
  libayatana-appindicator3-dev libpulse-dev libcurl4-openssl-dev libjson-glib-dev
./native/linux/build.sh --test
./native/linux/install.sh
~/.local/bin/keyscribe
```

`install.sh` installs the binary and application launcher under `~/.local` without
root. Run **KeyScribe** from Ubuntu's applications menu. Closing the window keeps
the app running in the tray; choose **종료** (Quit) in the tray to exit. Ubuntu's
AppIndicator extension must be enabled to show the tray icon. Settings are also
accessible by launching the app again. Only one app instance runs per session.

Create an installable package with `./packaging/build-deb.sh`, optionally setting
`KEYSCRIBE_VERSION`. The resulting `.deb` is in `dist-native/linux/` and can be
installed with `sudo apt install ./dist-native/linux/KeyScribe-ubuntu-*.deb`.
Ubuntu-only releases use `linux-vMAJOR.MINOR.PATCH` tags and are built on Ubuntu
24.04 in GitHub Actions before publication.

## Desktop portal setup

Install both backends when switching between GNOME and KDE Plasma:

```sh
sudo apt install xdg-desktop-portal xdg-desktop-portal-gnome xdg-desktop-portal-kde
```

Then log out and back in. Each desktop selects its own backend using its
shipped desktop-specific portal configuration. Keep both installed; a global
`portals.conf` that forces GNOME for KDE cannot supply KWin keyboard control.
KDE's GTK fallback alone supplies neither GlobalShortcuts nor RemoteDesktop.
The `.deb` recommends both backends and the local installer reports missing ones.
KDE keyboard injection resolves logical keys using the active GDK/XKB keymap
and sends portal keycodes, preserving custom modifier mappings. Chord releases
are briefly paced so asynchronous KWin/IBus handling retains their modifiers. GNOME retains
portal keysym injection.

Each desktop may ask for its own shortcut and keyboard permission on first use;
permissions granted by GNOME do not authorize KDE. The configured recording key
is retained across the switch. KDE uses its native system tray. Keyboard permission restore tokens are kept
separately for GNOME and KDE so switching cannot overwrite the other grant.

## First use

1. Enter an OpenAI (`sk-`), ElevenLabs (`sk_`) or Groq (`gsk_`) API key in Settings
   and save. Environment variables `ELEVENLABS_API_KEY`, `GROQ_API_KEY`, and
   `OPENAI_API_KEY` also work. API calls use the selected provider's account.
2. Choose one recording trigger in Settings (including **Hangul**). **키 지정**
   captures a key and closes immediately. The single **적용** button at the bottom
   saves all settings and registers a changed recording key. **닫기** discards
   unapplied edits and closes the window. The default is
   **Ctrl+Alt+Space**. The desktop's system
   dialog decides the actual binding; the app displays the returned shortcut.
3. Enable automatic paste in the same tab and accept the system keyboard-control
   permission. Only keyboard access is requested, with no screen capture. Grant
   it before recording so a permission dialog does not move the text cursor.
   Permission restoration is saved privately and requested again at launch,
   so an accepted persistent grant survives application restarts.
   If GNOME closes the keyboard session while the app is running, KeyScribe
   requests restoration using that saved grant and holds completed results until
   restoration finishes. Failed restoration shows the permission error. A shortcut
   press without keyboard permission requests access before recording; after
   accepting, return to the target text field and press the shortcut again.
   If permission is lost during transcription, the result is copied to the
   clipboard and Settings stays closed so the target keeps focus.
4. Focus the target text field, hold the recording shortcut, speak, then release.
   Holding it for more than a second stops recording on release; a shorter tap keeps
   recording until the next press. The recording button and tray menu always use toggle mode. Recording started from the app UI produces a
   result for manual copying, because that UI has keyboard focus.

On GNOME and KDE Plasma Wayland the app uses the **GlobalShortcuts**, **RemoteDesktop** (keyboard),
and, where available, **Clipboard** desktop portals. It also works on X11 when
the desktop provides these portals. There is no root input daemon or unrestricted
keyboard hook. Permissions last for the running session. If a portal is missing
or permission is denied/revoked, record with the app button, copy the result,
and paste it manually with Ctrl+V. Older GNOME versions may lack GlobalShortcuts;
use the button/tray in that case. On GNOME 50, the bundled KeyScribe Escape
extension cancels recording/transcription before Escape reaches the focused app.
The native installer enables it; log out and back in once after first installation.
For a Debian package installation, enable “KeyScribe recording cancellation” in
Extensions after logging back in. KDE Plasma 6 can use the native KWin Escape helper below. It consumes the logical
Escape press, repeats and release during recording/transcription, including while
the recording key or other modifiers are held. Idle Escape reaches the focused app.
Other desktops require the app/tray cancel action or Escape in the main app.

The default recording-start sound volume is 100%. It uses the current output
route and respects system speaker mute. If the volume is nonzero but the chime is
silent, check the desktop's output mute and selected speaker first. Recording
output mute starts after the chime and restores the previous output state.

Configured launches start in the tray; use the tray's Settings item or
`keyscribe --settings` to open the window explicitly.

Some Korean keyboards report the key labelled Hangul as `Alt_R` (evdev key 100,
XKB key 108). On the local GNOME desktop, a bare right-Alt portal binding emitted
presses without releases. Enabling the XKB option `korean:ralt_hangul`, preserving
the existing options, maps that physical key to Hangul for hold recording. The
local GNOME session required reloading input sources before the new map took
effect. The resulting keymap and both portal events were checked with Mutter's
keyboard session API; a short press started and stopped recording without an API
upload. The local original keyboard options are backed up in
`~/.config/keyscribe/keyboard-map-backup.json`.

On GNOME Wayland, IBus processes text-field keys before global shortcuts. The
Hangul engine normally consumes `Hangul` to change language, which prevents the
recording shortcut from firing while typing in a browser. While Hangul is the
recording trigger, KeyScribe reserves it by removing that key from the IBus
language-toggle bindings. Other toggles, including Shift+Space, stay available.
Original bindings are saved to `~/.config/keyscribe/ibus-shortcut-backup.ini` and
restored on exit or when selecting a different trigger. The next launch recovers
the backup after an interrupted run. Independent user preference edits are preserved.

After granting keyboard permission, `native/linux/test.sh --input` checks actual
desktop permission restoration, Korean clipboard paste, and Enter in a separate
native test entry. It aborts if that entry loses focus and sends no network messages. Use `--input --authorize` for an explicit first
keyboard permission request in the private test window.

## Features

The shared `vMAJOR.MINOR.PATCH` release workflow builds and tests Ubuntu amd64,
packages `KeyScribe-ubuntu-amd64-MAJOR.MINOR.PATCH.deb`, and attaches it to the same
GitHub Release as macOS and Windows. The publish job requires all three OS builds
and checks that the Ubuntu asset exists before publishing.

This port follows the existing macOS/Windows implementation:

- Model selector with asynchronous provider model-list loading, refresh, and a
  separately remembered selection for each provider; language selector.
- Hold-or-tap recording on one shortcut, cancellation, 10/20/30/60-minute limits, and original
  start/limit sounds with the same 0–200% gain and limiter.
- Original 260×52 dark rounded recording widget, five animated input-level bars,
  elapsed time, all eight positions, and the original tray icon in idle/red/orange.
- Output mute while recording and restoration of the previous mute state.
- Recognition words (one per line, up to 100), ElevenLabs filler removal, ordered
  text replacements, and the same key-token grammar (`[enter]`, `[cmd+k]`, etc.).
  On Linux, `cmd` maps to Ctrl, as it does on Windows. Unknown tokens remain text.
- New recordings can start during transcription. Completed results wait until
  recording stops and are delivered in recording order. Paste segments are paced
  so an Enter/chord runs between the intended text chunks.
- Native API-key links, settings save/cancel, tray restart, logs/recordings folder,
  retry on transient failures, and recording/log retention (1 hour/1/7/30 days).
- Large OpenAI/Groq recordings split into 9-minute WAVs. Original WAVs remain on
  API failure or cancellation. Files are created with mode 0600, directories 0700.

GNOME/Wayland routes global shortcuts and keyboard input through system portals.
The shortcut picker requests a key combination; GNOME decides the final binding.
The GNOME extension handles Escape directly in Shell, independently of the
recording portal session, so changing cancellation grabs preserves a held
recording key’s release. It releases its Escape grabs when idle and retains a
cancelled Escape until key-up. GNOME’s default Escape window/panel shortcuts
are temporarily routed to cancellation without changing desktop preferences.
Run `native/linux/test.sh --escape` for a separate headless GNOME/Wayland session
that verifies held recording keys, toggle cancellation, modifier Escape,
swallowed press/release, and normal Escape delivery while idle.
The positioned non-activating widget uses GTK through Ubuntu's XWayland; it
receives no keyboard or pointer input. Its background, text, waveform and margins
follow XWayland DPI scaling. On KDE the widget adds 20% to the DPI-scaled
size for readability (about 436×87 physical pixels at 140% when GTK uses an
X11 scale of one). The main settings app remains Wayland native.
Existing macOS and Windows implementations are unchanged.

## KDE Plasma 6 Escape helper

Build the optional helper against development headers for the **exact installed
KWin version** and Qt 6. KWin's input-filter interface is private, so rebuild this
helper when KWin changes; do not distribute a locally built helper for other versions.
The standard GTK build does not require these dependencies.

```sh
# kwin-dev and Qt 6 development packages must match the installed desktop.
python3 native/linux/kwin-extension/build.py
./native/linux/install.sh
```

If headers are extracted rather than installed, set `KEYSCRIBE_KWIN_INCLUDE_DIR`
to the extracted `usr/include` directory. The installer includes the helper when
built and enables its small declarative KWin bootstrap to discover user plugins
at login. Build with the same `KEYSCRIBE_INSTALL_PREFIX` used for installation.
Log in again after the initial helper installation, or load the bootstrap in KWin.
KeyScribe also requests helper loading at startup. The helper observes KeyScribe's
D-Bus recording state, releases its cancellation state when the app exits, and
keeps a consumed Escape held through key-up without touching the recording shortcut.

Quit idle KeyScribe, then run `native/linux/test.sh --kde-escape` in a live KDE
session. The private test injects keys only while its test window has focus,
checks held recording-key releases, toggle/modifier cancellation and idle Escape,
and unloads its temporary driver on completion. It does not record or upload audio.
GNOME's isolated equivalent is `native/linux/test.sh --escape`.

## Local files and checks

Preferences: `${XDG_CONFIG_HOME:-~/.config}/keyscribe/config.toml`.
API key: `user_config.json` in the same directory (property `api_key`).
Both files are written atomically with mode `0600`. Preferences use the same TOML
keys and string arrays as macOS/Windows, with Linux-only `shortcuts_enabled`.
The bundled [tomlc17 parser](vendor/tomlc17/README.md) needs no installed TOML package.
On startup, absent TOML is automatically migrated from `config.ini`, then
`settings.ini`; old files remain unchanged for recovery and may contain old keys.
New TOML wins over both legacy files. Invalid TOML or credential JSON stops startup
with an error instead of overwriting files or falling back to old values.
`keyscribe --config-path` reports the active path without migrating or opening GUI.
Recordings/diagnostic log: `${XDG_DATA_HOME:-~/.local/share}/keyscribe/logs/`
(`recording-*.wav` and `debug.log`).
API keys are stored locally in plaintext with user-only file permissions. The app
never prints keys or transcripts to diagnostic output. Recordings are uploaded
only when a recording is finished for transcription. Cancellation stops the
request but cannot recall audio already uploaded. Remove these folders after
quitting to delete local data.

`./native/linux/test.sh` runs core, local mock-HTTP/provider-model, portal,
key-command, and original-widget rendering tests without API keys.
`./native/linux/test.sh --desktop` additionally opens a native window and records
three seconds from the real microphone, cancels without uploading, and verifies
the WAV, Korean clipboard, mute restoration, selectors, and ordered results. Run this optional check in a live desktop session.

Portal API references: [GlobalShortcuts](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html),
[RemoteDesktop](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html),
[Clipboard](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Clipboard.html).
