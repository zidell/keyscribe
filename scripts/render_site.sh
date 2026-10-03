#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 && $# -ne 5 ]] || ! [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo 'Usage: render_site.sh MAJOR.MINOR.PATCH OUTPUT_DIRECTORY [MACOS_ARM64_SIZE MACOS_X64_SIZE WINDOWS_SIZE]' >&2
    exit 1
fi

version="$1"
output_dir="$2"
macos_arm64_size="${3:-~0.3 MB}"
macos_x64_size="${4:-~0.3 MB}"
windows_size="${5:-~12.18 MB}"
project_root="$(cd "$(dirname "$0")/.." && pwd)"
# Ubuntu는 linux-v* 태그로 따로 릴리스하므로, 가장 높은 태그 버전을 다운로드 링크에 쓴다.
linux_version="${KEYSCRIBE_LINUX_VERSION:-$(git -C "$project_root" ls-remote --tags --refs origin 'refs/tags/linux-v*' |
    sed 's#.*refs/tags/linux-v##' | sort -V | tail -n 1)}"
if ! [[ "$linux_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Ubuntu 릴리스 버전을 찾지 못했습니다: '$linux_version'" >&2
    exit 1
fi
mkdir -p "$output_dir"
sed -e "s/__VERSION__/$version/g" \
    -e "s/__LINUX_VERSION__/$linux_version/g" \
    -e "s/__MACOS_ARM64_SIZE__/$macos_arm64_size/g" \
    -e "s/__MACOS_X64_SIZE__/$macos_x64_size/g" \
    -e "s/__WINDOWS_SIZE__/$windows_size/g" \
    "$project_root/site/index.html" > "$output_dir/index.html"
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
printf '%s\n' "$version" > "$output_dir/version.txt"
