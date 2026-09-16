#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 || ! "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo 'Usage: render_site.sh MAJOR.MINOR.PATCH OUTPUT_DIRECTORY' >&2
    exit 1
fi

version="$1"
output_dir="$2"
project_root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$output_dir"
sed "s/__VERSION__/$version/g" "$project_root/site/index.html" > "$output_dir/index.html"
cp "$project_root/site/_worker.js" "$output_dir/_worker.js"
cp "$project_root/assets/keyscribe.svg" "$output_dir/logo.svg"
printf '%s\n' "$version" > "$output_dir/version.txt"
