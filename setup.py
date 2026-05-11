from setuptools import setup

APP = ["main.py"]
DATA_FILES = ["config.json"]
OPTIONS = {
    "argv_emulation": False,
    "plist": {
        "CFBundleName": "voice-stt",
        "CFBundleDisplayName": "voice-stt",
        "CFBundleIdentifier": "com.videostew.voice-stt",
        "CFBundleVersion": "1.0.0",
        "LSUIElement": True,  # Dock에 아이콘 숨김 (메뉴바 전용)
        "NSMicrophoneUsageDescription": "음성 녹음을 위해 마이크 접근이 필요합니다.",
        "NSAccessibilityUsageDescription": "키보드 단축키 감지를 위해 접근성 권한이 필요합니다.",
    },
    "packages": [
        "rumps",
        "pynput",
        "sounddevice",
        "numpy",
        "pyperclip",
        "elevenlabs",
        "dotenv",
        "httpx",
        "certifi",
    ],
    "includes": ["AppKit", "Foundation"],
}

setup(
    app=APP,
    data_files=DATA_FILES,
    options={"py2app": OPTIONS},
    setup_requires=["py2app"],
)
