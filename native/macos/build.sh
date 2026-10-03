#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/../.." && pwd)"
mac_root="$project_root/native/macos"
output_dir="${KEYSCRIBE_BUILD_DIR:-$project_root/dist-native}"
output="$output_dir/KeyScribe.app"

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
cp "$project_root/assets/recording-limit.wav" "$staged_app/Contents/Resources/recording-limit.wav"
cp "$project_root/docs/readme.txt" "$staged_app/Contents/Resources/readme.txt"
cp "$project_root/config.toml.example" "$staged_app/Contents/Resources/config.toml.example"
mkdir -p "$staged_app/Contents/Frameworks"
# ditto keeps the framework's Versions symlinks intact.
ditto "$mac_root/.build/release/Sparkle.framework" "$staged_app/Contents/Frameworks/Sparkle.framework"

if [[ -n "${KEYSCRIBE_VERSION:-}" ]]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $KEYSCRIBE_VERSION" "$staged_app/Contents/Info.plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $KEYSCRIBE_VERSION" "$staged_app/Contents/Info.plist"
    # Only versioned release builds look for updates; each architecture has its own feed
    # so an update never swaps an Apple Silicon app for an Intel build or vice versa.
    case "$(lipo -archs "$staged_app/Contents/MacOS/KeyScribe")" in
        arm64) feed_arch=arm64 ;;
        x86_64) feed_arch=x64 ;;
        *) echo '업데이트 피드를 정할 수 없는 아키텍처입니다.' >&2; exit 1 ;;
    esac
    /usr/libexec/PlistBuddy -c "Add :SUFeedURL string https://keyscribe.gitools.net/appcast-$feed_arch.xml" \
        "$staged_app/Contents/Info.plist"
fi

if [[ -n "${KEYSCRIBE_CODESIGN_IDENTITY:-}" ]]; then
    # Sparkle's helpers are signed one by one, innermost first; --deep would strip
    # the Downloader service's entitlements.
    sparkle="$staged_app/Contents/Frameworks/Sparkle.framework"
    codesign_sparkle() { codesign --force --options runtime --timestamp --sign "$KEYSCRIBE_CODESIGN_IDENTITY" "$@"; }
    codesign_sparkle "$sparkle/Versions/B/XPCServices/Installer.xpc"
    codesign_sparkle --preserve-metadata=entitlements "$sparkle/Versions/B/XPCServices/Downloader.xpc"
    codesign_sparkle "$sparkle/Versions/B/Autoupdate"
    codesign_sparkle "$sparkle/Versions/B/Updater.app"
    codesign_sparkle "$sparkle"
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
