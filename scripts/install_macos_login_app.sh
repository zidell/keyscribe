#!/bin/bash
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    echo 'macOS에서만 사용할 수 있습니다.' >&2
    exit 1
fi

project_root="$(cd "$(dirname "$0")/.." && pwd)"
app="$project_root/dist-native/KeyScribe.app/Contents/MacOS/KeyScribe"
if [[ ! -x "$app" ]]; then
    echo "앱이 없습니다. 먼저 bash native/macos/build.sh를 실행하세요: $app" >&2
    exit 1
fi

label='net.gitools.keyscribe.dev'
agent="$HOME/Library/LaunchAgents/$label.plist"
domain="gui/$(id -u)"

xml_escape() {
    local value="$1"
    value="${value//&/\&amp;}"
    value="${value//</\&lt;}"
    value="${value//>/\&gt;}"
    value="${value//\"/\&quot;}"
    printf '%s' "$value"
}

mkdir -p "$(dirname "$agent")" "$project_root/dist-native"
cat > "$agent" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>Label</key><string>$label</string>
    <key>ProgramArguments</key><array>
        <string>$(xml_escape "$app")</string>
    </array>
    <key>WorkingDirectory</key><string>$(xml_escape "$project_root")</string>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>ThrottleInterval</key><integer>10</integer>
    <key>StandardOutPath</key><string>$(xml_escape "$project_root/dist-native/app.log")</string>
    <key>StandardErrorPath</key><string>$(xml_escape "$project_root/dist-native/app.log")</string>
</dict></plist>
EOF

plutil -lint "$agent"
launchctl bootout "$domain/$label" 2>/dev/null || true
for ((attempt = 0; attempt < 5; attempt++)); do
    if launchctl bootstrap "$domain" "$agent" 2>/dev/null; then
        echo "로그인 시 네이티브 앱 실행을 설치했습니다: $agent"
        exit 0
    fi
    sleep 0.5
done
echo "로그인 에이전트를 시작하지 못했습니다: $agent" >&2
exit 1
