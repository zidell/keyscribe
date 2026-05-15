import subprocess
import py2app.util

# py2app의 _dosign이 --preserve-metadata=runtime 플래그로 인해
# 일부 바이너리 서명에 실패하는 문제를 우회한다.
def _simple_dosign(*path):
    subprocess.check_call(("codesign", "-s", "-", "-f") + path)

py2app.util._dosign = _simple_dosign

from setuptools import setup

APP = ["main.py"]
DATA_FILES = []
OPTIONS = {
    "argv_emulation": False,
    "plist": {
        "CFBundleName": "voice-stt",
        "CFBundleDisplayName": "voice-stt",
        "CFBundleIdentifier": "com.videostew.voice-stt",
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
    "includes": ["AppKit", "Foundation"],
    # Tkinter 제외: Tk/Tcl.framework가 번들에 포함되면 .a 파일 서명 실패
    "excludes": ["_tkinter", "tkinter", "Tkinter", "test"],
}

setup(
    app=APP,
    data_files=DATA_FILES,
    options={"py2app": OPTIONS},
    setup_requires=["py2app"],
)
