#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/../.." && pwd)"
mac_root="$project_root/native/macos"
output="$project_root/dist-native/KeyScribe.app"

swift build --package-path "$mac_root" -c release
mkdir -p "$output/Contents/MacOS" "$output/Contents/Resources"
cp "$mac_root/.build/release/KeyScribe" "$output/Contents/MacOS/KeyScribe"
cp "$mac_root/Info.plist" "$output/Contents/Info.plist"
cp "$project_root/assets/keyscribe-menu.png" "$output/Contents/Resources/keyscribe-menu.png"
cp "$project_root/assets/keyscribe.icns" "$output/Contents/Resources/keyscribe.icns"
cp "$project_root/config.toml.example" "$output/Contents/Resources/config.toml.example"

if [[ -n "${KEYSCRIBE_VERSION:-}" ]]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $KEYSCRIBE_VERSION" "$output/Contents/Info.plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $KEYSCRIBE_VERSION" "$output/Contents/Info.plist"
fi

echo "$output"
