#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

magick -background none -density 384 "$project_root/assets/keyscribe-menu.svg" \
    -resize 64x64 "$project_root/assets/keyscribe-menu.png"
magick -background none -density 384 "$project_root/assets/keyscribe.svg" \
    -resize 1024x1024 "$work_dir/app.png"

iconset="$work_dir/KeyScribe.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$work_dir/app.png" \
        --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double_size=$((size * 2))
    sips -z "$double_size" "$double_size" "$work_dir/app.png" \
        --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$project_root/assets/keyscribe.icns"
