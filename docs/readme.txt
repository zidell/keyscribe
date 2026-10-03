KeyScribe — installed app settings guide / 설치된 앱 환경설정 안내
================================================================

This readme.txt is shipped with the app. No repository access or network
connection is needed to use the instructions below.

Installed guide locations:
- macOS: KeyScribe.app/Contents/Resources/readme.txt
- Windows: readme.txt beside KeyScribe.exe (including inside the MSIX package)
- Linux .deb: /usr/share/doc/keyscribe/readme.txt
- Linux local install: <install prefix>/share/doc/keyscribe/readme.txt

한국어 안내: 아래 OS별 실제 사용자 설정 파일을 수정하세요. 설치 폴더의 파일은
안내 문서입니다. 앱을 종료한 뒤 설정을 편집하고, 다시 실행해야 반영됩니다.

KeyScribe stores preferences in editable UTF-8 text files. Use this guide when a
user asks to change KeyScribe's language, recording behavior, shortcut, widget,
sound, recognition words, or replacement rules. There is currently no
file watcher or automatic reload after an external edit.

## Locate the installed user's files

| Platform | Preferences | API key |
| --- | --- | --- |
| macOS | `~/Library/Application Support/keyscribe/config.toml` | `user_config.json` in the same directory, property `api_key` |
| Windows | `%APPDATA%\keyscribe\config.toml` | `user_config.json` in the same directory, property `api_key` |
| Linux | `${XDG_CONFIG_HOME:-~/.config}/keyscribe/config.toml` | `user_config.json` in the same directory, property `api_key` |

Resolve paths in the environment of the account running the installed app.
Windows packaged apps may redirect application data; if the file is absent at
the normal location, locate the existing `keyscribe` directory in that app's
package data before creating a second configuration. On macOS, first launch
creates the annotated preferences file if missing. Windows and Linux also create
an annotated default file on first launch, without copying environment API keys
into it. Existing files are not replaced during startup.
Bundled `config.toml.example` files are templates, not the live preferences.

## Find settings from the installed executable

Run `keyscribe --config-path` (Linux), `KeyScribe.exe --config-path` (Windows), or
`/Applications/KeyScribe.app/Contents/MacOS/KeyScribe --config-path` (macOS).
Use the actual installed executable path if it differs. This prints the active
preferences path without starting the GUI or requesting permissions. `--help`
also documents discovery. Agents should capture stdout; on Windows, redirect or
pipe stdout when invoking a GUI executable from a console.

Each saved preferences file includes comments explaining its fields, so it can
be edited without reading this guide. Comments are refreshed when the GUI saves.
Linux automatically migrates legacy `config.ini`, or `settings.ini` if no
`config.ini` exists, on normal startup when `config.toml` is absent. Preferences
move to commented TOML with the same key names as macOS/Windows. Stored API keys
move to `user_config.json`; other JSON properties are preserved. Original INI
files remain unchanged for recovery and may still contain the old API key.
`config.toml` always takes precedence after migration. `--config-path` reports
legacy paths before migration and TOML afterward, without performing migration.
An invalid TOML or credentials JSON stops Linux startup rather than replacing
files or silently loading old values. Fix the indicated syntax or value and retry.

## Change and apply

1. Identify the OS and the existing preferences file. Read only the values needed
   for the request. Legacy Linux INI files contain the API key; do not dump
   credentials files or legacy INI contents into tool output or a response.
2. Have KeyScribe idle and quit it before editing to avoid an in-memory settings
   save overwriting the edit. Do not interrupt an active recording or transcription.
3. Back up the existing file privately, then change only the requested keys.
   Preserve unrelated values, comments, and API credentials. Add a missing key
   once; avoid duplicate keys. Keep credentials and Linux backups private
   (mode `0600` on Unix). Prefer replacing the file atomically.
4. Validate types, supported values, and syntax. Invalid settings may silently
   fall back to defaults on macOS/Windows; Linux reports an error. Use the
   common syntax below even if a general TOML
   library supports more features.
5. Relaunch the installed app. If editing while it is running was unavoidable,
   use the menu's Restart action before any Settings save. Opening Settings and
   clicking Save does **not** reload externally edited preferences.
6. Confirm the file contains the requested values and, when possible, check the
   relaunched Settings window. Report what changed and whether the app was
   relaunched; do not claim runtime verification from a file check alone.

GUI saves rewrite the preferences file. Generated comments describe each field,
but arbitrary user comments are currently not preserved by GUI saves.

## Preference names and values

Defaults below are the code defaults. An existing file or the macOS bundled
template can supply different values; keep those unless the user requests a change.

| Meaning | Key on all platforms | Values / default |
| --- | --- | --- |
| Transcription language (also UI language where supported) | `language` | String, e.g. `"ko"`, `"en"`, `"ja"`; default `ko` |
| Recording time limit | `recording_time_limit_minutes` | Integer: `10`, `20`, `30`, `60`; default `30` |
| Log and recording retention | `log_retention_hours` | Integer hours: `1`, `24`, `168`, `720`; default `168` |
| Press Enter after pasting | `auto_send` | Boolean; default `true` |
| Mute system output while recording | `mute_during_recording` | Boolean; default `true` |
| Recording start sound volume | `recording_start_sound_volume` | Integer percent, `0`–`200`; `0` disables it; default `100`; Linux sound follows system output mute |
| Recording widget placement | `overlay_position` | `hidden`, `top_left`, `top_center`, `top_right`, `center`, `bottom_left`, `bottom_center`, `bottom_right`; default `bottom_center` |
| Remove filler words | `no_verbatim` | Boolean; default `true`; sent only to ElevenLabs `scribe_v2` / `scribe_v2_medical` |
| OpenAI model | `openai_model` | String; code default `gpt-transcribe` on macOS/Windows, `gpt-4o-mini-transcribe` on Linux |
| ElevenLabs model | `elevenlabs_model` | String; default `scribe_v2` |
| Groq model | `groq_model` | String; default `whisper-large-v3-turbo` |
| Recognition words | `keyterms` | Array of individual strings, at most 100; default `[]`; commas within a word are literal |
| Replacement rules | `replacements` | Ordered array of individual rule strings; default `[]` |
| Recording trigger | `shortcut` | Platform-specific, see below. A tap records until the next press; holding over one second stops on release |
| Linux shortcut authorization state | `shortcuts_enabled` | Linux only; app-managed boolean, default `false`; do not edit to bypass portal authorization |

Provider selection comes from the API-key prefix (`sk-`, `sk_`, `gsk_`), not from
the model name. Changing a model alone does not switch providers. Leave credentials
alone unless the user requests an API-key/provider change. Never print the key.
When no stored key is present, the app checks `ELEVENLABS_API_KEY`, `GROQ_API_KEY`,
then `OPENAI_API_KEY` in its process environment. A desktop launch may have a
different environment from the agent's shell.

### Shortcuts

- macOS: `right_command` (default), `left_cmd`, `right_option`, `left_option`,
  `right_ctrl`, `left_ctrl`, `right_shift`, `left_shift`.
- Windows: `right_alt`, `left_alt`, `right_ctrl`, `left_ctrl`, `right_shift`,
  `left_shift`. The default `right_option` is an alias for `right_alt`.
- Linux (GNOME and KDE Plasma): requires xdg-desktop-portal and the matching
  xdg-desktop-portal-gnome / xdg-desktop-portal-kde backend. Keep both installed
  when switching desktops; log in again after installing a missing backend.
  Shortcut and keyboard permissions are granted separately by each desktop;
  their keyboard permission restore tokens are stored separately.
  Global Escape cancellation uses the bundled GNOME Shell extension or the
  optional KWin 6 helper built against the installed KWin version; KDE helper
  installation is described in linux-readme.md beside this guide.
  Portal accelerator string, default `CTRL+ALT+space`. A changed shortcut
  may require desktop portal approval; use the app's shortcut registration flow
  if needed. A file edit cannot grant keyboard/paste permissions.

### Shared TOML syntax

Use top-level `key = value` assignments, double-quoted strings, lowercase
`true`/`false`, integer numbers, and arrays kept on a single line. macOS uses a
limited parser: avoid TOML sections, single-quoted strings, and multiline arrays.
Use JSON-compatible escaping inside strings. `#` comments are supported.

For “turn off automatic sending and the sound, and hide the widget,” edit:

```toml
auto_send = false
recording_start_sound_volume = 0
overlay_position = "hidden"
```

For recognition words and replacement rules:

```toml
keyterms = ["KeyScribe", "VideoStew"]
replacements = ["키스크라이브 => KeyScribe", "비디오 스튜 => VideoStew"]
```

### Linux migration field mapping

Do not edit legacy INI after TOML has been created; it is a recovery copy.
The migration maps:

| Legacy INI field | TOML field |
| --- | --- |
| `hold` | dropped; recording mode now follows how long the shortcut is held |
| `sound_volume` | `recording_start_sound_volume` |
| `limit_minutes` | `recording_time_limit_minutes` |
| `retention_hours` | `log_retention_hours` |
| comma/newline `keyterms` | array of individual `keyterms` strings |
| newline `replacements` | ordered array of individual `replacements` strings |
| `api_key` | JSON property `api_key` in `user_config.json` |

All other existing preference fields keep their names. Linux's internal shortcut
authorization state stays in the Linux-only `shortcuts_enabled` field.
Linux supports standard TOML via a bundled parser, including multiline arrays and
literal strings. For shared edits across OSes use the restricted syntax above,
since macOS currently has a smaller parser. Array items must be individual
single-line words/rules; use separate entries instead of embedded line breaks.

Replacement rules run in order and accept `=>` or `->`; an empty right side
deletes matching text. On all three platforms, recognized bracket tokens such as
`[enter]` and `[cmd+k]` can execute keystrokes. Add those only when the user asks
for that behavior. On Linux, `cmd` maps to Ctrl.

For older installed releases, inspect the existing settings before assuming
every key is available. This guide ships with the app version it describes.
