#!/bin/bash
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    echo 'macOS에서만 사용할 수 있습니다.' >&2
    exit 1
fi

project_root="$(cd "$(dirname "$0")/.." && pwd)"
label='net.gitools.keyscribe.source-watch'
agent="$HOME/Library/LaunchAgents/$label.plist"
domain="gui/$(id -u)"
mkdir -p "$(dirname "$agent")" "$project_root/dist-native"
if [[ ! -f "$project_root/dist-native/source.sha" ]]; then
    bash "$project_root/scripts/macos_source_hash.sh" > "$project_root/dist-native/source.sha"
fi

xml_escape() {
    local value="$1"
    value="${value//&/\&amp;}"
    value="${value//</\&lt;}"
    value="${value//>/\&gt;}"
    value="${value//\"/\&quot;}"
    printf '%s' "$value"
}

{
    cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>Label</key><string>$label</string>
    <key>ProgramArguments</key><array>
        <string>/bin/bash</string>
        <string>$(xml_escape "$project_root/scripts/auto_rebuild_macos_app.sh")</string>
    </array>
    <key>WorkingDirectory</key><string>$(xml_escape "$project_root")</string>
    <key>RunAtLoad</key><true/>
    <key>StartInterval</key><integer>10</integer>
    <key>WatchPaths</key><array>
EOF
    for path in "$project_root/native/macos/Package.swift" \
                "$project_root/native/macos/Info.plist" \
                "$project_root/native/macos/build.sh" \
                "$project_root/config.toml.example" \
                "$project_root/packaging/macos-entitlements.plist"; do
        printf '        <string>%s</string>\n' "$(xml_escape "$path")"
    done
    while IFS= read -r -d '' path; do
        printf '        <string>%s</string>\n' "$(xml_escape "$path")"
    done < <(find -s "$project_root/native/macos/Sources" "$project_root/assets" -print0)
    cat <<EOF
    </array>
    <key>StandardOutPath</key><string>$(xml_escape "$project_root/dist-native/watch.log")</string>
    <key>StandardErrorPath</key><string>$(xml_escape "$project_root/dist-native/watch.log")</string>
</dict></plist>
EOF
} > "$agent"

plutil -lint "$agent"
launchctl bootout "$domain/$label" 2>/dev/null || true
for ((attempt = 0; attempt < 5; attempt++)); do
    if launchctl bootstrap "$domain" "$agent" 2>/dev/null; then
        echo "macOS 소스 변경 자동 빌드를 설치했습니다: $agent"
        exit 0
    fi
    sleep 0.5
done
echo "소스 감시 작업을 시작하지 못했습니다: $agent" >&2
exit 1
