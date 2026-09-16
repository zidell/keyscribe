import os
import re
import subprocess
import py2app.util

# py2app의 _dosign이 --preserve-metadata=runtime 플래그로 인해
# 일부 바이너리 서명에 실패하는 문제를 우회한다.
def _signing_identity():
    configured = os.environ.get("KEYSCRIBE_CODESIGN_IDENTITY")
    if configured:
        return configured
    try:
        result = subprocess.run(
            ("security", "find-identity", "-v", "-p", "codesigning"),
            check=True, capture_output=True, text=True,
        )
        developer_ids = re.findall(
            r'\b[0-9A-F]{40}\s+"(Developer ID Application: [^"]+)"',
            result.stdout,
        )
        if len(developer_ids) == 1:
            return developer_ids[0]
    except (OSError, subprocess.CalledProcessError):
        pass
    print("KeyScribe: Developer ID 인증서를 하나로 특정할 수 없어 임시 서명합니다. "
          "접근성 권한을 빌드마다 유지하려면 KEYSCRIBE_CODESIGN_IDENTITY를 설정하세요.")
    return "-"


SIGNING_IDENTITY = _signing_identity()


def _simple_dosign(*path):
    subprocess.check_call(("codesign", "-s", SIGNING_IDENTITY, "-f") + path)

py2app.util._dosign = _simple_dosign

from setuptools import setup

APP = ["main.py"]
# i18n.py는 런타임에 직접 import하므로 번들 Resources에도 명시적으로 포함한다.
DATA_FILES = [("", ["i18n.py", "config.toml.example"]),
              ("assets", ["assets/keyscribe-menu.png"])]
OPTIONS = {
    "iconfile": "assets/keyscribe.icns",
    "argv_emulation": False,
    "plist": {
        "CFBundleName": "KeyScribe",
        "CFBundleDisplayName": "KeyScribe",
        "CFBundleIdentifier": "com.videostew.keyscribe",
        "CFBundleVersion": "1.0.0",
        "LSUIElement": True,
        "NSMicrophoneUsageDescription": "음성 녹음을 위해 마이크 접근이 필요합니다.",
        "NSAccessibilityUsageDescription": "키보드 단축키 감지를 위해 접근성 권한이 필요합니다.",
    },
    "packages": [
        "rumps",
        "pynput",
        "sounddevice",
        "_sounddevice_data",  # libportaudio.dylib 포함 — zip 안에 넣으면 dlopen 불가
        "numpy",
        "pyperclip",
        "elevenlabs",
        "dotenv",
        "httpx",
        "certifi",
    ],
    "includes": ["AppKit", "Foundation", "ApplicationServices", "i18n"],
    # Tkinter 제외: Tk/Tcl.framework가 번들에 포함되면 .a 파일 서명 실패
    "excludes": ["_tkinter", "tkinter", "Tkinter", "test"],
}

setup(
    app=APP,
    data_files=DATA_FILES,
    options={"py2app": OPTIONS},
    setup_requires=["py2app"],
)
