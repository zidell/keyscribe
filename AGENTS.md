# KeyScribe

For requests to change an installed app's preferences, read
[docs/agent-settings.md](docs/agent-settings.md) first. It describes the actual
per-user files, platform-specific keys, syntax, and how to apply changes.
Do not edit `config.toml.example` to change a user's installed app settings.

When changing settings code, keep that guide and the generated configuration
comments consistent with the platform loaders and defaults.

For reusable cross-platform design principles and blind-agent test results, see
[Agent Configuration Accessibility](https://github.com/zidell/agent-configuration-accessibility).

## Maintainer's local macOS dev loop

The maintainer runs the `dist-native/KeyScribe.app` build as a login
LaunchAgent and rebuilds it automatically on source changes. Those scripts are
personal and live only in the gitignored `local/` directory, so a fresh clone
does not have them. When moving to another machine, copy `local/` along and run
`bash local/install_macos_login_app.sh` once. The app's only dependency on this
setup is the `KEYSCRIBE_KEEPALIVE=1` relaunch check in
`native/macos/Sources/KeyScribe/main.swift`.
