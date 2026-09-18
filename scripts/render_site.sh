#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 && $# -ne 5 ]] || ! [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo 'Usage: render_site.sh MAJOR.MINOR.PATCH OUTPUT_DIRECTORY [MACOS_ARM64_SIZE MACOS_X64_SIZE WINDOWS_SIZE]' >&2
    exit 1
fi

version="$1"
output_dir="$2"
macos_arm64_size="${3:-출시 후 표시}"
macos_x64_size="${4:-출시 후 표시}"
windows_size="${5:-출시 후 표시}"
project_root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$output_dir"
sed -e "s/__VERSION__/$version/g" \
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
touch "$output_dir/.nojekyll"
printf '%s\n' "$version" > "$output_dir/version.txt"
