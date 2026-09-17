#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/../.." && pwd)"
mac_root="$project_root/native/macos"
output_dir="$project_root/dist-native"
output="$output_dir/KeyScribe.app"

local_identity='Developer ID Application: heunghyun lee (AF68GKBM82)'
if [[ -z "${KEYSCRIBE_CODESIGN_IDENTITY:-}" ]] &&
    security find-identity -v -p codesigning | grep -Fq "$local_identity"; then
    KEYSCRIBE_CODESIGN_IDENTITY="$local_identity"
fi
if [[ -z "${KEYSCRIBE_CODESIGN_IDENTITY:-}" && -d "$output" ]] &&
    codesign -dv --verbose=2 "$output" 2>&1 | grep -Eq '^TeamIdentifier=[A-Z0-9]+'; then
    echo '서명된 앱을 서명 없는 빌드로 덮어쓸 수 없습니다. 서명 인증서를 확인해 주세요.' >&2
    exit 1
fi

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
cp "$project_root/assets/recording-start.wav" "$staged_app/Contents/Resources/recording-start.wav"
cp "$project_root/config.toml.example" "$staged_app/Contents/Resources/config.toml.example"

if [[ -n "${KEYSCRIBE_VERSION:-}" ]]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $KEYSCRIBE_VERSION" "$staged_app/Contents/Info.plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $KEYSCRIBE_VERSION" "$staged_app/Contents/Info.plist"
fi

if [[ -n "${KEYSCRIBE_CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --options runtime --timestamp \
        --entitlements "$project_root/packaging/macos-entitlements.plist" \
        --sign "$KEYSCRIBE_CODESIGN_IDENTITY" "$staged_app"
    codesign --verify --deep --strict "$staged_app"
fi

if [[ -e "$output" ]]; then
    mv "$output" "$staging_root/previous.app"
fi
mv "$staged_app" "$output"

echo "$output"
