# KeyScribe

[![CI](https://github.com/zidell/keyscribe/actions/workflows/ci.yml/badge.svg)](https://github.com/zidell/keyscribe/actions/workflows/ci.yml) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) [![codecov](https://codecov.io/gh/zidell/keyscribe/graph/badge.svg)](https://codecov.io/gh/zidell/keyscribe)

[English](README.md) | [한국어](README.ko.md) | [中文](README.zh-CN.md) | [日本語](README.ja.md) | [Español](README.es.md)

KeyScribe is a voice-input app that transcribes microphone recordings and pastes the resulting text into the active input field. It deliberately keeps the feature set small and is implemented natively, with an idle-memory target of roughly 20 MB. It does not include its own model, so an OpenAI, ElevenLabs, or Groq API key is required (Groq can be used for free).

## Features

- Supports push-to-talk and toggle recording, Escape to cancel, and a recording time limit.
- Shows recording and transcription status plus live input level in a small widget, placed at the bottom center of the screen by default.
- Lets you change the shortcut, language, model, sound-effect volume, recording-widget position, and automatic Enter key in Settings.
- **View Logs** in the menu opens diagnostic logs from the last 24 hours. API keys and recorded or transcribed content are never written to the logs.

## Why I made it

There are already many voice-input apps. After trying a number of paid and free options and reading user experiences on Reddit, I found that local STT models still fell short on accuracy. For everyday use, OpenAI and ElevenLabs STT APIs seemed the most accurate and reliable choices. Paid apps often cost too much for what they offered, required frequent bug-fix updates because of feature bloat, or used more than 100 MB of memory while idle. Some free apps did not meet expectations for polish or distribution quality.

## Preview

The images below show the app's control menu and usage flow.

### Control menu

| macOS | Windows |
| --- | --- |
| ![KeyScribe menu open from the macOS menu bar](assets/screenshots/macos-menu-preview.png) | ![KeyScribe menu open from the Windows tray](assets/screenshots/windows-menu-preview.png) |

### Usage flow

| macOS | Windows |
| --- | --- |
| ![KeyScribe recording, transcribing, and automatically pasting on macOS](assets/screenshots/keyscribe-flow.gif) | ![KeyScribe recording, transcribing, and automatically pasting on Windows](assets/screenshots/keyscribe-windows-flow.gif) |

## Install

See the [KeyScribe download page](https://keyscribe.gitools.net) for downloads and installation instructions. All installers are distributed directly through [GitHub Releases](https://github.com/zidell/keyscribe/releases).

| Operating system | File | Install or run |
| --- | --- | --- |
| macOS Apple Silicon | `KeyScribe-macos-arm64-*.dmg` | Open the DMG and copy the app to Applications |
| macOS Intel | `KeyScribe-macos-x64-*.dmg` | Open the DMG and copy the app to Applications |
| Windows 10/11 x64 | `KeyScribe-windows-x64-*.msix` | Download the SignPath-signed installer from GitHub Releases |

## First use

1. Open **Settings...** from the KeyScribe menu-bar or tray icon, then choose an ElevenLabs, OpenAI, or Groq API key and model. Groq's default model is `whisper-large-v3-turbo`.
2. On macOS, allow Microphone and **System Settings → Privacy & Security → Accessibility** permissions. On Windows, allow desktop-app microphone access under **Settings → Privacy & security → Microphone**.
3. Put the cursor in the field where you want text, then hold Right Command (macOS) or Right Alt (Windows) while speaking. Release the key to paste the transcription.

In Settings you can change the shortcut, recording-stop method, recording time limit (10, 20, 30, or 60 minutes; 30 by default), recognition language, recording-start sound volume, and automatic sending. With automatic sending enabled, KeyScribe presses Enter after pasting. To automatically type into a Windows app running as administrator, KeyScribe may also need to run as administrator.

## STT providers

The provider is selected automatically from the API-key prefix. The last selected model is saved separately for each provider.

| Provider | API key prefix | Default model |
| --- | --- | --- |
| OpenAI | `sk-` | `gpt-transcribe` |
| ElevenLabs | `sk_` | `scribe_v2` |
| Groq | `gsk_` | `whisper-large-v3-turbo` |

Groq uses an OpenAI-compatible transcription API. Get an API key through **Groq Key ↗** in Settings; after entering it, you can load the model list.

### Getting an API key

Paste a key created with the steps below into **Settings... → API Key** in KeyScribe, then click **Refresh**. Treat a key like a password: never share it or post it publicly.

The route to key creation differs by service. Notably, a ChatGPT subscription is separate from OpenAI API billing, and ElevenLabs requires a Full Seat to create a personal API key. Groq has the lowest barrier to trying it for free, but its free tier has usage and rate limits.

#### OpenAI

1. Sign in or create an account on the [OpenAI API keys page](https://platform.openai.com/api-keys).
2. Set up an API payment method or credits under **Billing** in the API Platform. A ChatGPT Plus or Pro subscription alone does not include API usage.
3. Select the project to use. Individual users can keep the default project.
4. Click **Create new secret key**, name it, and create the key.
5. Copy the displayed `sk-…` key into KeyScribe.

OpenAI API keys are created per project; permissions and usage limits can be managed in project settings. [Official OpenAI guide](https://help.openai.com/en/articles/9186755)

#### ElevenLabs

1. Sign in or create an account on the [ElevenLabs API keys page](https://elevenlabs.io/app/developers/api-keys).
2. Confirm that you have the **Full Seat** required to create a personal API key. Otherwise, the appropriate plan or workspace-admin setting is needed.
3. Create a key in the personal API-key list and give it a recognizable name.
4. Copy the resulting `sk_…` key into KeyScribe.

Personal keys can be created and rotated in personal API-key settings. [Official ElevenLabs guide](https://elevenlabs.io/docs/overview/administration/workspaces/api-keys)

#### Groq

1. Sign in or create an account on the [Groq Console API keys page](https://console.groq.com/keys). Keys can be created on the free tier.
2. If this is your first time, create a project or select the default project from the project selector.
3. Click **Create API Key**, name it, and create the key.
4. Copy the resulting `gsk_…` key into KeyScribe.

Groq keys belong to the selected project, and the free tier has request and audio-processing limits. A payment method is required to move to the paid Developer tier for higher limits. [Official Groq project guide](https://console.groq.com/docs/projects), [billing guide](https://console.groq.com/docs/billing-faqs)

## Troubleshooting

You can check app status and errors in the menu-bar or tray menu. **View Logs** in that menu opens the app's diagnostic log.

- macOS: `~/Library/Application Support/keyscribe/debug.log`
- Windows: `%APPDATA%\keyscribe\debug.log`
- Development runs: `dist-native/macos-debug.log`, `dist-native/windows-debug.log`

Logs for the macOS login app and source-watch build are stored in `dist-native/app.log` and `dist-native/watch.log`.

The [privacy notice](docs/privacy.md) explains how audio is sent and API keys are stored.

The source code is available under the [MIT License](LICENSE).
