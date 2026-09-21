# KeyScribe

[![CI](https://github.com/zidell/keyscribe/actions/workflows/ci.yml/badge.svg)](https://github.com/zidell/keyscribe/actions/workflows/ci.yml) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) [![codecov](https://codecov.io/gh/zidell/keyscribe/graph/badge.svg)](https://codecov.io/gh/zidell/keyscribe)

[English](README.md) | [한국어](README.ko.md) | [中文](README.zh-CN.md) | [日本語](README.ja.md) | [Español](README.es.md)

KeyScribe 是一款语音输入应用：它将麦克风录音转写为文字，并粘贴到当前输入框。应用以原生方式实现，只保留必要功能，目标是在空闲时使用约 20 MB 内存。它不包含自有模型，因此需要 OpenAI、ElevenLabs 或 Groq API 密钥（Groq 可免费使用）。

## 功能

- 支持按住说话、切换录音、Esc 取消和录音时间限制。
- 以小部件显示录音和转写状态及实时输入音量，默认位于屏幕底部中央。
- 可在设置中更改快捷键、语言、模型、提示音音量、录音小部件位置和自动按 Enter。
- 菜单中的**查看日志**可打开最近 24 小时的诊断日志；API 密钥和录音、转写内容不会写入日志。

## 为什么制作它

市面上已有很多语音输入应用。我试用了多款付费和免费产品，也参考了 Reddit 的使用体验。当地 STT 模型的准确性仍不够理想；在日常使用中，OpenAI 与 ElevenLabs 的 STT API 是最准确、稳定的选择。付费应用往往价格偏高、功能膨胀导致更新频繁，或在空闲时使用超过 100 MB 内存；部分免费应用的完成度或发布质量也不符合预期。

## 预览

下图展示应用的控制菜单和使用流程。

### 控制菜单

| macOS | Windows |
| --- | --- |
| ![macOS 菜单栏中打开的 KeyScribe 菜单](assets/screenshots/macos-menu-preview.png) | ![Windows 托盘中打开的 KeyScribe 菜单](assets/screenshots/windows-menu-preview.png) |

### 使用流程

| macOS | Windows |
| --- | --- |
| ![KeyScribe 在 macOS 上录音、转写并自动粘贴](assets/screenshots/keyscribe-flow.gif) | ![KeyScribe 在 Windows 上录音、转写并自动粘贴](assets/screenshots/keyscribe-windows-flow.gif) |

## 安装

请在 [KeyScribe 下载页面](https://keyscribe.gitools.net)查看下载和安装说明。所有安装文件均直接通过 [GitHub Releases](https://github.com/zidell/keyscribe/releases) 发布。

| 操作系统 | 文件 | 安装或运行 |
| --- | --- | --- |
| macOS Apple Silicon | `KeyScribe-macos-arm64-*.dmg` | 打开 DMG 并将应用复制到 Applications |
| macOS Intel | `KeyScribe-macos-x64-*.dmg` | 打开 DMG 并将应用复制到 Applications |
| Windows 10/11 x64 | `KeyScribe-windows-x64-*.msix` | 从 GitHub Releases 下载经 SignPath 签名的安装程序 |

## 首次使用

1. 从菜单栏或托盘的 KeyScribe 图标打开**设置...**，选择 ElevenLabs、OpenAI 或 Groq API 密钥和模型。Groq 默认模型为 `whisper-large-v3-turbo`。
2. macOS 请允许麦克风及**系统设置 → 隐私与安全性 → 辅助功能**权限。Windows 请在**设置 → 隐私和安全性 → 麦克风**中允许桌面应用访问麦克风。
3. 将光标放到需要输入文字的位置，按住右 Command（macOS）或右 Alt（Windows）说话。松开按键即可粘贴转写结果。

可在设置中更改快捷键、录音停止方式、录音时限（10、20、30、60 分钟；默认 30 分钟）、识别语言、录音开始提示音音量和自动发送。启用自动发送后，粘贴完成会按 Enter。若要向以管理员身份运行的 Windows 应用自动输入，KeyScribe 也可能需要以管理员身份运行。

## STT 提供商

应用会根据 API 密钥前缀自动选择提供商，并分别保存每个提供商最后选择的模型。

| 提供商 | API 密钥前缀 | 默认模型 |
| --- | --- | --- |
| OpenAI | `sk-` | `gpt-transcribe` |
| ElevenLabs | `sk_` | `scribe_v2` |
| Groq | `gsk_` | `whisper-large-v3-turbo` |

Groq 使用兼容 OpenAI 的转写 API。可通过设置中的 **Groq 密钥 ↗** 创建 API 密钥；输入后即可载入模型列表。

### 获取 API 密钥

将按以下步骤创建的密钥粘贴到 KeyScribe 的**设置... → API 密钥**，然后点按**刷新**。密钥和密码一样重要，请勿分享或公开发布。

ChatGPT 订阅与 OpenAI API 计费相互独立；ElevenLabs 创建个人 API 密钥需要 Full Seat。Groq 最易免费开始使用，但免费层有用量和速率限制。

#### OpenAI

1. 登录或注册 [OpenAI API 密钥页面](https://platform.openai.com/api-keys)。
2. 在 API Platform 的 **Billing** 中设置 API 付款方式或额度；ChatGPT Plus 或 Pro 不含 API 用量。
3. 选择项目（个人用户可使用默认项目）。
4. 点击 **Create new secret key**，命名后创建密钥。
5. 将显示的 `sk-…` 密钥复制到 KeyScribe。

OpenAI API 密钥按项目创建，可在项目设置中管理权限和用量限制。[OpenAI 官方说明](https://help.openai.com/en/articles/9186755)

#### ElevenLabs

1. 登录或注册 [ElevenLabs API 密钥页面](https://elevenlabs.io/app/developers/api-keys)。
2. 确认拥有创建个人 API 密钥所需的 **Full Seat**；否则需相应套餐或工作区管理员设置。
3. 在个人 API 密钥列表中新建密钥并命名。
4. 将生成的 `sk_…` 密钥复制到 KeyScribe。

个人密钥可在个人 API 密钥设置中创建和轮换。[ElevenLabs 官方说明](https://elevenlabs.io/docs/overview/administration/workspaces/api-keys)

#### Groq

1. 登录或注册 [Groq Console API 密钥页面](https://console.groq.com/keys)；免费层也可创建密钥。
2. 首次使用时，在项目选择器中新建或选择默认项目。
3. 点击 **Create API Key**，命名后创建密钥。
4. 将生成的 `gsk_…` 密钥复制到 KeyScribe。

Groq 密钥归属于所选项目，免费层有请求和音频处理限制。提高额度的付费 Developer 层需要付款方式。[Groq 项目说明](https://console.groq.com/docs/projects)，[计费说明](https://console.groq.com/docs/billing-faqs)

## 故障排除

可在菜单栏或托盘菜单查看应用状态和错误；**查看日志**会打开诊断日志。

- macOS：`~/Library/Application Support/keyscribe/debug.log`
- Windows：`%APPDATA%\keyscribe\debug.log`
- 开发运行：`dist-native/macos-debug.log`、`dist-native/windows-debug.log`

macOS 登录应用和源代码监视构建的日志位于 `dist-native/app.log`、`dist-native/watch.log`。

[隐私说明](docs/privacy.md)介绍音频传输和 API 密钥存储方式。源代码采用 [MIT 许可证](LICENSE)发布。
