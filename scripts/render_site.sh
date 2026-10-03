#!/bin/bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo 'Usage: render_site.sh OUTPUT_DIRECTORY' >&2
    echo 'Optional: KEYSCRIBE_WINDOWS_STORE_URL once the Microsoft Store listing is live, GH_TOKEN for API limits.' >&2
    exit 1
fi

output_dir="$1"
project_root="$(cd "$(dirname "$0")/.." && pwd)"
repo='zidell/keyscribe'

# Each platform ships from its own tag series (macos-v*, linux-v*); link the newest of each.
latest_version() {
    local version
    version="$(git -C "$project_root" ls-remote --tags --refs origin "refs/tags/$1-v*" |
        sed "s#.*refs/tags/$1-v##" | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n 1)"
    if [[ -z "$version" ]]; then
        echo "$1 릴리스 버전을 찾지 못했습니다" >&2
        exit 1
    fi
    printf '%s' "$version"
}
macos_version="$(latest_version macos)"
linux_version="$(latest_version linux)"

auth=()
if [[ -n "${GH_TOKEN:-}" ]]; then auth=(-H "Authorization: Bearer $GH_TOKEN"); fi
macos_release="$(curl -fsSL ${auth[@]+"${auth[@]}"} "https://api.github.com/repos/$repo/releases/tags/macos-v$macos_version")"
asset_size() {
    jq -r --arg name "$1" '.assets[] | select(.name == $name) | .size' <<< "$macos_release" |
        awk '{ printf "%.1f MB", $1 / 1000000 }'
}
macos_arm64_size="$(asset_size "KeyScribe-macos-arm64-$macos_version.dmg")"
macos_x64_size="$(asset_size "KeyScribe-macos-x64-$macos_version.dmg")"

mkdir -p "$output_dir"
sed -e "s/__MACOS_VERSION__/$macos_version/g" \
    -e "s/__LINUX_VERSION__/$linux_version/g" \
    -e "s/__MACOS_ARM64_SIZE__/${macos_arm64_size:-DMG}/g" \
    -e "s/__MACOS_X64_SIZE__/${macos_x64_size:-DMG}/g" \
    "$project_root/site/index.html" > "$output_dir/index.html"
# Windows is distributed only through the Microsoft Store; until the listing is live the
# page says it is coming instead of linking to it.
if [[ -n "${KEYSCRIBE_WINDOWS_STORE_URL:-}" ]]; then
    store_url="${KEYSCRIBE_WINDOWS_STORE_URL//&/\\&}"
    sed -i.bak -e '/data-windows-pending/d' -e "s#__WINDOWS_STORE_URL__#$store_url#g" "$output_dir/index.html"
else
    sed -i.bak -e '/data-windows-store/d' "$output_dir/index.html"
fi
rm -f "$output_dir/index.html.bak"

# Sparkle in the macOS app reads appcast-<arch>.xml; the Linux app compares linux-version.txt.
for arch in arm64 x64; do
    if ! curl -fsL -o "$output_dir/appcast-$arch.xml" \
        "https://github.com/$repo/releases/download/macos-v$macos_version/appcast-$arch.xml"; then
        rm -f "$output_dir/appcast-$arch.xml"
        echo "macos-v$macos_version 릴리스에 appcast-$arch.xml이 없어 업데이트 피드를 건너뜁니다." >&2
    fi
done
printf '%s\n' "$linux_version" > "$output_dir/linux-version.txt"

cp "$project_root/assets/keyscribe.svg" "$output_dir/logo.svg"
mkdir -p "$output_dir/screenshots"
cp "$project_root/assets/screenshots/keyscribe-flow.gif" "$output_dir/screenshots/keyscribe-flow.gif"
cp "$project_root/assets/screenshots/keyscribe-windows-flow.gif" "$output_dir/screenshots/keyscribe-windows-flow.gif"
cp "$project_root/assets/screenshots/macos-menu-preview.png" "$output_dir/screenshots/macos-menu-preview.png"
cp "$project_root/assets/screenshots/windows-menu-preview.png" "$output_dir/screenshots/windows-menu-preview.png"
cp "$project_root/site/CNAME" "$output_dir/CNAME"
cp "$project_root/site/robots.txt" "$output_dir/robots.txt"
cp "$project_root/site/sitemap.xml" "$output_dir/sitemap.xml"
touch "$output_dir/.nojekyll"
