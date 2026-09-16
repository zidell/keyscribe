#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/../.." && pwd)"
mac_root="$project_root/native/macos"
output_dir="$project_root/dist-native"
output="$output_dir/KeyScribe.app"

swift build --package-path "$mac_root" -c release
mkdir -p "$output_dir"
staging_root="$(mktemp -d "$output_dir/.keyscribe-build.XXXXXX")"
staged_app="$staging_root/KeyScribe.app"
cleanup() {
    if [[ -d "$staging_root/previous.app" && ! -e "$output" ]]; then
        mv "$staging_root/previous.app" "$output"
    fi
    find "$staging_root" -depth -delete
}
trap cleanup EXIT

mkdir -p "$staged_app/Contents/MacOS" "$staged_app/Contents/Resources"
cp "$mac_root/.build/release/KeyScribe" "$staged_app/Contents/MacOS/KeyScribe"
cp "$mac_root/Info.plist" "$staged_app/Contents/Info.plist"
cp "$project_root/assets/keyscribe-menu.png" "$staged_app/Contents/Resources/keyscribe-menu.png"
cp "$project_root/assets/keyscribe.icns" "$staged_app/Contents/Resources/keyscribe.icns"
cp "$project_root/config.toml.example" "$staged_app/Contents/Resources/config.toml.example"

if [[ -n "${KEYSCRIBE_VERSION:-}" ]]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $KEYSCRIBE_VERSION" "$staged_app/Contents/Info.plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $KEYSCRIBE_VERSION" "$staged_app/Contents/Info.plist"
fi

if [[ -n "${KEYSCRIBE_CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --options runtime \
        --entitlements "$project_root/packaging/macos-entitlements.plist" \
        --sign "$KEYSCRIBE_CODESIGN_IDENTITY" "$staged_app"
    codesign --verify --deep --strict "$staged_app"
fi

if [[ -e "$output" ]]; then
    mv "$output" "$staging_root/previous.app"
fi
mv "$staged_app" "$output"

echo "$output"
