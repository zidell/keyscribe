#!/bin/bash
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    echo 'macOS에서만 사용할 수 있습니다.' >&2
    exit 1
fi

project_root="$(cd "$(dirname "$0")/.." && pwd)"
identity='Developer ID Application: heunghyun lee (AF68GKBM82)'
if [[ -z "${KEYSCRIBE_CODESIGN_IDENTITY:-}" ]] &&
    security find-identity -v -p codesigning | grep -Fq "$identity"; then
    export KEYSCRIBE_CODESIGN_IDENTITY="$identity"
fi

bash "$project_root/native/macos/build.sh"

service="gui/$(id -u)/net.gitools.keyscribe.dev"
if launchctl print "$service" >/dev/null 2>&1; then
    launchctl kickstart -k "$service"
else
    bash "$project_root/scripts/install_macos_login_app.sh"
fi
echo '네이티브 앱을 빌드하고 다시 실행했습니다.'
