# KeyScribe for Ubuntu

Ubuntu support is an additional native implementation alongside `native/macos`
and `native/windows`. This app is compiled C with GTK 3, Ayatana AppIndicator,
PulseAudio (including PipeWire's PulseAudio server), libcurl, and JSON-GLib.
There is no webview or bundled browser runtime.

## Build and install

Target: Ubuntu 24.04 or newer. Build packages on the oldest Ubuntu release you
intend to distribute them to. A binary built on a newer system may require newer
libraries. Ubuntu 26.04 GNOME/Wayland is the local development environment.

Download the Ubuntu amd64 `.deb` and its SHA-256 checksum from
[KeyScribe Ubuntu 0.1.1](https://github.com/zidell/keyscribe/releases/tag/linux-v0.1.1).
Open the downloaded package to install, or run
`sudo apt install ./KeyScribe-ubuntu-amd64-0.1.1.deb`.
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

## First use

1. Enter an OpenAI (`sk-`), ElevenLabs (`sk_`) or Groq (`gsk_`) API key in Settings
   and save. Environment variables `ELEVENLABS_API_KEY`, `GROQ_API_KEY`, and
   `OPENAI_API_KEY` also work. API calls use the selected provider's account.
2. Choose one recording trigger in Settings (including **Hangul**). **키 지정**
   captures a key and closes immediately. The single **적용** button at the bottom
   saves all settings and registers a changed recording key. **닫기** discards
   unapplied edits and closes the window. The default is
   **Ctrl+Alt+Space**. GNOME's system
   dialog decides the actual binding; the app displays the returned shortcut.
3. Enable automatic paste in the same tab and accept the system keyboard-control
   permission. Only keyboard access is requested, with no screen capture. Grant
   it before recording so a permission dialog does not move the text cursor.
   Permission restoration is saved privately and requested again at launch,
   so an accepted persistent grant survives application restarts.
   If GNOME closes the keyboard session while the app is running, KeyScribe
   requests restoration using that saved grant and holds completed results until
   restoration finishes. Failed restoration shows the permission error; the next
   completed shortcut recording opens Settings with the result available to copy.
4. Focus the target text field, hold the recording shortcut, speak, then release.
   Select toggle in the recording-mode picker for toggle recording. The recording button and tray
   menu always use toggle mode. Recording started from the app UI produces a
   result for manual copying, because that UI has keyboard focus.

On Wayland the app uses the **GlobalShortcuts**, **RemoteDesktop** (keyboard),
and, where available, **Clipboard** desktop portals. It also works on X11 when
the desktop provides these portals. There is no root input daemon or unrestricted
keyboard hook. Permissions last for the running session. If a portal is missing
or permission is denied/revoked, record with the app button, copy the result,
and paste it manually with Ctrl+V. Older GNOME versions may lack GlobalShortcuts;
use the button/tray in that case. Escape cancels while the app window has focus;
cancel from the app or tray menu.

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
GNOME permission restoration, Korean clipboard paste, and Enter in a separate
native test entry. It aborts if that entry loses focus and sends no network messages.

## Features

The shared `vMAJOR.MINOR.PATCH` release workflow builds and tests Ubuntu amd64,
packages `KeyScribe-ubuntu-amd64-MAJOR.MINOR.PATCH.deb`, and attaches it to the same
GitHub Release as macOS and Windows. The publish job requires all three OS builds
and checks that the Ubuntu asset exists before publishing.

This port follows the existing macOS/Windows implementation:

- Model selector with asynchronous provider model-list loading, refresh, and a
  separately remembered selection for each provider; language selector.
- Hold/toggle recording, cancellation, 10/20/30/60-minute limits, and original
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
Single modifier hooks such as Windows Right Alt and global bare-Escape capture
are unavailable through these portals. Cancel from the app or tray menu instead.
The positioned non-activating widget uses GTK through Ubuntu's XWayland; it
receives no keyboard or pointer input. The main settings app remains Wayland native.
Existing macOS and Windows implementations are unchanged.

## Local files and checks

Settings/API key: `${XDG_CONFIG_HOME:-~/.config}/keyscribe/settings.ini`.
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
