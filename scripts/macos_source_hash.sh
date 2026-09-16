#!/bin/bash
set -euo pipefail
export LC_ALL=C

project_root="$(cd "$(dirname "$0")/.." && pwd)"
{
    find -s "$project_root/native/macos/Sources" "$project_root/assets" \
        -type f -print0 | xargs -0 shasum
    shasum "$project_root/native/macos/Package.swift" \
        "$project_root/native/macos/Info.plist" \
        "$project_root/native/macos/build.sh" \
        "$project_root/config.toml.example" \
        "$project_root/packaging/macos-entitlements.plist"
} | shasum | cut -d ' ' -f 1
