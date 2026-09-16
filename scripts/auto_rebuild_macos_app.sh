#!/bin/bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
state="$project_root/dist-native/source.sha"
mkdir -p "$project_root/dist-native"

# Editors often write the same file several times in quick succession.
sleep 0.8
current="$(bash "$project_root/scripts/macos_source_hash.sh")"
if [[ -f "$state" && "$(cat "$state")" == "$current" ]]; then
    exit 0
fi

# Remember failed attempts too; the next source edit triggers another build.
printf '%s\n' "$current" > "$state"
echo 'macOS 소스 변경을 감지해 앱을 다시 빌드합니다.'
bash "$project_root/scripts/rebuild_macos_app.sh"
