import os
import re
import sys
import json
import math
import time
import queue
import shutil
import logging
import threading
import tempfile
import ctypes
import ctypes.util
import subprocess
from i18n import set_ui_language, tr
from logging.handlers import TimedRotatingFileHandler
from pathlib import Path

try:
    import tomllib
except ImportError:
    import tomli as tomllib  # Python 3.10 호환

import soundfile as sf

PLATFORM = sys.platform  # 'darwin' | 'win32'
APP_NAME = "KeyScribe"
APP_SLUG = "keyscribe"
LEGACY_APP_SLUG = "voice-stt"

# ------------------------------------------------------------------ #
# 플랫폼별 imports
# ------------------------------------------------------------------ #
if PLATFORM == "darwin":
    try:
        import rumps
    except ImportError:
        rumps = None

elif PLATFORM == "win32":
    try:
        import pystray
        from PIL import Image as PILImage, ImageDraw as PILDraw
    except ImportError:
        pystray = None
        PILImage = None
        PILDraw = None
    try:
        import tkinter as tk
        import tkinter.messagebox as tkmsgbox
        from tkinter import ttk
    except ImportError:
        tk = None

# ------------------------------------------------------------------ #
# 로거 설정
# ------------------------------------------------------------------ #
def _get_log_dir() -> Path:
    if PLATFORM == "darwin":
        return Path.home() / "Library" / "Logs" / APP_SLUG
    elif PLATFORM == "win32":
        appdata = os.environ.get("APPDATA", str(Path.home()))
        return Path(appdata) / APP_SLUG / "Logs"
    else:
        return Path.home() / ".local" / "share" / APP_SLUG / "logs"

_LOG_DIR = _get_log_dir()
_LOG_DIR.mkdir(parents=True, exist_ok=True)

_log_handler = TimedRotatingFileHandler(
    _LOG_DIR / f"{APP_SLUG}.log",
    when="midnight",
    backupCount=1,
    encoding="utf-8",
)
_log_handler.setFormatter(logging.Formatter(
    "%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
))
log = logging.getLogger(APP_SLUG)
log.setLevel(logging.DEBUG)
log.addHandler(_log_handler)

def _log_uncaught_exception(exc_type, exc_value, exc_traceback):
    """메인 스레드의 예외가 조용히 사라지지 않도록 항상 파일에 남긴다."""
    if issubclass(exc_type, KeyboardInterrupt):
        sys.__excepthook__(exc_type, exc_value, exc_traceback)
        return
    log.critical("처리되지 않은 메인 스레드 예외", exc_info=(exc_type, exc_value, exc_traceback))

def _log_thread_exception(args):
    """녹음·전사 등 백그라운드 스레드의 예외를 파일 로그에 기록한다."""
    log.critical("처리되지 않은 스레드 예외 — thread=%s", args.thread.name,
                 exc_info=(args.exc_type, args.exc_value, args.exc_traceback))

sys.excepthook = _log_uncaught_exception
threading.excepthook = _log_thread_exception

log.info("=" * 60)
log.info("앱 시작 — Python %s | platform=%s | frozen=%s | pid=%d",
         sys.version.split()[0], PLATFORM, getattr(sys, "frozen", False), os.getpid())
log.debug("sys.executable: %s", sys.executable)
log.debug("sys.argv: %s", sys.argv)

# ------------------------------------------------------------------ #
# SSL 패치 (macOS .app 빌드용)
# ------------------------------------------------------------------ #
import ssl
import certifi as _certifi_mod
try:
    _ca_path = _certifi_mod.where()
    log.debug("certifi CA 파일: %s", _ca_path)
    with open(_ca_path, "r", encoding="ascii") as _f:
        _CA_DATA = _f.read()
    _orig_create_ssl_context = ssl.create_default_context
    def _certifi_ssl_context(purpose=ssl.Purpose.SERVER_AUTH, *, cafile=None, capath=None, cadata=None):
        if cadata is None:
            cadata = _CA_DATA
            cafile = None
            capath = None
        return _orig_create_ssl_context(purpose, cafile=cafile, capath=capath, cadata=cadata)
    ssl.create_default_context = _certifi_ssl_context
    log.info("SSL certifi 패치 완료")
except Exception:
    log.exception("SSL certifi 패치 실패")

# ------------------------------------------------------------------ #
# 공통 패키지 imports
# ------------------------------------------------------------------ #
try:
    import numpy as np
    log.info("numpy 로드 OK — version=%s", np.__version__)
except Exception:
    log.exception("numpy 로드 실패")
    np = None

try:
    import pyperclip
    log.info("pyperclip 로드 OK")
except Exception:
    log.exception("pyperclip 로드 실패")
    pyperclip = None

try:
    import sounddevice as sd
    log.info("sounddevice 로드 OK — version=%s", sd.__version__)
    try:
        log.debug("기본 입력 장치: %s", sd.query_devices(kind='input'))
    except Exception:
        log.debug("기본 입력 장치 조회 실패")
except Exception:
    log.exception("sounddevice 로드 실패")
    sd = None

try:
    from elevenlabs.client import ElevenLabs
    log.info("elevenlabs 로드 OK")
except Exception:
    log.exception("elevenlabs 로드 실패")
    ElevenLabs = None

try:
    import httpx
    log.info("httpx 로드 OK — version=%s", httpx.__version__)
except Exception:
    log.exception("httpx 로드 실패")
    httpx = None


class _STTResult:
    """STT 결과의 최소 컨테이너 — 기존 result.text 접근과 호환."""
    __slots__ = ("text",)
    def __init__(self, text: str):
        self.text = text

try:
    from pynput import keyboard
    log.info("pynput 로드 OK")
except Exception:
    log.exception("pynput 로드 실패")
    keyboard = None

# ------------------------------------------------------------------ #
# 경로
# ------------------------------------------------------------------ #
RESOURCE_DIR = Path(os.path.dirname(os.path.abspath(__file__)))
CONFIG_EXAMPLE_PATH = RESOURCE_DIR / "config.toml.example"
MENU_ICON_PATH = RESOURCE_DIR / "assets" / "keyscribe-menu.png"

def _get_user_config_path() -> Path:
    if PLATFORM == "darwin":
        return Path.home() / "Library" / "Application Support" / APP_SLUG / "user_config.json"
    elif PLATFORM == "win32":
        appdata = os.environ.get("APPDATA", str(Path.home()))
        return Path(appdata) / APP_SLUG / "user_config.json"
    else:
        return Path.home() / ".config" / APP_SLUG / "user_config.json"

def _get_legacy_user_config_path() -> Path:
    if PLATFORM == "darwin":
        return Path.home() / "Library" / "Application Support" / LEGACY_APP_SLUG / "user_config.json"
    elif PLATFORM == "win32":
        appdata = os.environ.get("APPDATA", str(Path.home()))
        return Path(appdata) / LEGACY_APP_SLUG / "user_config.json"
    return Path.home() / ".config" / LEGACY_APP_SLUG / "user_config.json"

USER_CONFIG_PATH = _get_user_config_path()
LEGACY_USER_CONFIG_PATH = _get_legacy_user_config_path()
# 개발 실행은 프로젝트 설정을 사용하고, .app은 서명된 번들 밖의 사용자별 설정을 사용한다.
CONFIG_PATH = (USER_CONFIG_PATH.parent / "config.toml"
               if getattr(sys, "frozen", False) else RESOURCE_DIR / "config.toml")
log.info("CONFIG_PATH: %s", CONFIG_PATH)
log.debug("USER_CONFIG_PATH: %s", USER_CONFIG_PATH)

# ------------------------------------------------------------------ #
# 설정 로드/저장
# ------------------------------------------------------------------ #
def _ensure_config_exists():
    """config.toml이 없으면 config.toml.example을 복사한다 (첫 실행/새 컴퓨터)."""
    if CONFIG_PATH.exists():
        return
    if not CONFIG_EXAMPLE_PATH.exists():
        log.error("config.toml과 config.toml.example 둘 다 없음 — 설정 파일 누락")
        return
    try:
        shutil.copy2(CONFIG_EXAMPLE_PATH, CONFIG_PATH)
        log.info("config.toml 없음 — config.toml.example을 복사: %s", CONFIG_PATH)
    except Exception:
        log.exception("config.toml.example 복사 실패")

def _toml_format_value(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    elif isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    elif isinstance(v, (int, float)):
        return str(v)
    elif isinstance(v, list):
        return "[" + ", ".join(_toml_format_value(i) for i in v) + "]"
    return repr(v)

def _sync_missing_config_keys():
    """config.toml.example에 있지만 config.toml에 없는 키를 config.toml 끝에 추가한다."""
    if not CONFIG_EXAMPLE_PATH.exists() or not CONFIG_PATH.exists():
        return
    with open(CONFIG_EXAMPLE_PATH, "rb") as f:
        defaults = tomllib.load(f)
    with open(CONFIG_PATH, "rb") as f:
        user_data = tomllib.load(f)

    missing_scalars = [(k, v) for k, v in defaults.items() if k not in user_data and not isinstance(v, dict)]
    missing_tables  = [(k, v) for k, v in defaults.items() if k not in user_data and isinstance(v, dict)]

    if not missing_scalars and not missing_tables:
        return

    scalar_lines = [f"{k} = {_toml_format_value(v)}" for k, v in missing_scalars]
    table_lines = []
    for k, v in missing_tables:
        table_lines.append(f"\n[{k}]")
        table_lines.extend(f"{sk} = {_toml_format_value(sv)}" for sk, sv in v.items())

    # TOML의 키는 가장 최근 [table] 헤더에 속한다. 파일 끝에 scalar를 붙이면
    # 마지막 table의 키가 되어 다음 시작에서 다시 추가되는 문제가 생긴다.
    content = CONFIG_PATH.read_text(encoding="utf-8")
    first_table = re.search(r"(?m)^\s*\[[^\]]+\]\s*$", content)
    scalar_block = ("\n" + "\n".join(scalar_lines) + "\n") if scalar_lines else ""
    if first_table and scalar_block:
        content = content[:first_table.start()] + scalar_block + "\n" + content[first_table.start():]
    elif scalar_block:
        content += scalar_block
    if table_lines:
        content += "\n" + "\n".join(table_lines) + "\n"
    CONFIG_PATH.write_text(content, encoding="utf-8")

    added = [k for k, _ in missing_scalars] + [k for k, _ in missing_tables]
    log.info("config.toml 누락 키 자동 추가: %s", added)

def _deep_merge(base: dict, override: dict) -> dict:
    """override를 base 위에 재귀적으로 덮어씌운다. base에만 있는 키는 기본값으로 유지."""
    result = dict(base)
    for k, v in override.items():
        if k in result and isinstance(result[k], dict) and isinstance(v, dict):
            result[k] = _deep_merge(result[k], v)
        else:
            result[k] = v
    return result

def load_config() -> dict:
    _ensure_config_exists()
    defaults = {}
    if CONFIG_EXAMPLE_PATH.exists():
        with open(CONFIG_EXAMPLE_PATH, "rb") as f:
            defaults = tomllib.load(f)
    log.debug("config.toml 로드: %s", CONFIG_PATH)
    with open(CONFIG_PATH, "rb") as f:
        user_data = tomllib.load(f)
    data = _deep_merge(defaults, user_data)
    log.debug("config.toml 로드 완료 — 키: %s", list(data.keys()))
    return data

_CONFIG_KEYS = (
    "shortcut", "recording_control", "auto_send", "language", "keyterms",
    "no_verbatim", "mute_during_recording", "openai_model", "elevenlabs_model",
)

def save_config(data: dict):
    """지원하는 설정만 TOML로 저장해 폐기된 설정을 함께 정리한다."""
    values = {key: data[key] for key in _CONFIG_KEYS if key in data}
    lines = [f"{key} = {_toml_format_value(value)}" for key, value in values.items()]
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    log.info("config.toml 저장됨 — 키: %s", list(values))

def load_user_config() -> dict:
    if USER_CONFIG_PATH.exists():
        log.debug("user_config.json 로드: %s", USER_CONFIG_PATH)
        with open(USER_CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        log.debug("user_config.json 로드 완료 — 키: %s", list(data.keys()))
        return data
    if LEGACY_USER_CONFIG_PATH.exists():
        log.info("기존 %s 설정을 %s로 이전합니다.", LEGACY_APP_SLUG, APP_SLUG)
        with open(LEGACY_USER_CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        save_user_config(data)
        return data
    log.debug("user_config.json 없음 — 빈 dict 반환")
    return {}

def save_user_config(data: dict):
    USER_CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    log.debug("user_config.json 저장 — 키: %s", list(data.keys()))
    with open(USER_CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    log.debug("user_config.json 저장 완료")

def abbreviate_api_key(api_key: str) -> str:
    """설정 창에서만 API Key의 가운데를 감춰 표시한다."""
    if len(api_key) <= 30:
        return api_key
    return f"{api_key[:15]}....{api_key[-15:]}"

def provider_for_api_key(api_key: str) -> str | None:
    if api_key.startswith("sk-"):
        return "openai"
    if api_key.startswith("sk_"):
        return "elevenlabs"
    return None

def fetch_transcription_models(api_key: str) -> list[str]:
    """현재 API Key가 접근 가능한 파일 전사 모델만 반환한다."""
    provider = provider_for_api_key(api_key)
    if httpx is None or provider is None:
        return []
    if provider == "openai":
        response = httpx.get("https://api.openai.com/v1/models",
                             headers={"Authorization": f"Bearer {api_key}"}, timeout=10)
        response.raise_for_status()
        return sorted(item["id"] for item in response.json().get("data", [])
                      if ("transcribe" in item.get("id", "") or item.get("id") == "whisper-1")
                      and not re.search(r"-\d{4}-\d{2}-\d{2}$", item.get("id", "")))
    response = httpx.get("https://api.elevenlabs.io/v1/models",
                         headers={"xi-api-key": api_key}, timeout=10)
    response.raise_for_status()
    return sorted(item["model_id"] for item in response.json()
                  if item.get("model_id", "").startswith("scribe")
                  and not re.search(r"-\d{4}-\d{2}-\d{2}$", item.get("model_id", "")))

def settings_values(api_key: str) -> dict:
    config = load_config()
    return {
        "api_key": api_key,
        "shortcut": str(config.get("shortcut", "right_option")),
        "recording_control": str(config.get("recording_control", "hold")),
        # ptt_after_key를 쓰던 기존 설정은 Enter일 때만 자동 전송으로 이어받는다.
        "auto_send": bool(config.get("auto_send", config.get("ptt_after_key", "enter") == "enter")),
        "language": str(config.get("language", "ko")),
        "keyterms": "\n".join(str(term) for term in config.get("keyterms", [])),
        "no_verbatim": bool(config.get("no_verbatim", True)),
        "mute_during_recording": bool(config.get("mute_during_recording", True)),
        "openai_model": str(config.get("openai_model", "gpt-transcribe")),
        "elevenlabs_model": str(config.get("elevenlabs_model", "scribe_v2")),
        "model": str(config.get("openai_model" if provider_for_api_key(api_key) == "openai" else "elevenlabs_model", "gpt-transcribe" if provider_for_api_key(api_key) == "openai" else "scribe_v2")),
    }

def save_settings(values: dict):
    """설정 폼 값을 저장하고 API Key는 사용자별 파일에 분리 보관한다."""
    keyterms = [line.strip() for line in values["keyterms"].splitlines() if line.strip()]
    save_config({
        "shortcut": values["shortcut"].strip(),
        "recording_control": values["recording_control"],
        "auto_send": values["auto_send"],
        "language": values["language"].strip(),
        "keyterms": keyterms,
        "no_verbatim": values["no_verbatim"],
        "mute_during_recording": values["mute_during_recording"],
        "openai_model": values.get("openai_model", "gpt-transcribe"),
        "elevenlabs_model": values.get("elevenlabs_model", "scribe_v2"),
    })
    user_config = load_user_config()
    user_config["api_key"] = values["api_key"].strip()
    save_user_config(user_config)


def shortcut_options() -> list[tuple[str, str]]:
    """현재 OS에서 안정적으로 감지할 수 있는 push-to-talk 키 목록."""
    if PLATFORM == "darwin":
        return [
            ("right_option", "오른쪽 Option (⌥)"), ("left_option", "왼쪽 Option (⌥)"),
            ("right_cmd", "오른쪽 Command (⌘)"), ("left_cmd", "왼쪽 Command (⌘)"),
            ("right_ctrl", "오른쪽 Control (⌃)"), ("left_ctrl", "왼쪽 Control (⌃)"),
            ("right_shift", "오른쪽 Shift (⇧)"), ("left_shift", "왼쪽 Shift (⇧)"),
        ]
    return [
        ("right_alt", "오른쪽 Alt"), ("left_alt", "왼쪽 Alt"),
        ("right_ctrl", "오른쪽 Ctrl"), ("left_ctrl", "왼쪽 Ctrl"),
        ("right_shift", "오른쪽 Shift"), ("left_shift", "왼쪽 Shift"),
    ]


def recording_control_options() -> list[tuple[str, str]]:
    return [("hold", tr("recording_control_hold")),
            ("toggle", tr("recording_control_toggle"))]


UI_LANGUAGE_BY_SPEECH = {
    "ko": "ko", "en": "en", "ja": "ja", "zh": "zh-Hans", "es": "es",
}


def sync_ui_language(language: str | None = None) -> None:
    """전사 언어의 지역 변형까지 UI 언어로 정규화하고, 미지원 언어는 영어로 쓴다."""
    if language is None:
        language = str(load_config().get("language", "ko"))
    language_base = language.replace("_", "-").lower().split("-", 1)[0]
    set_ui_language(UI_LANGUAGE_BY_SPEECH.get(language_base, "en"))


def speech_language_options() -> list[tuple[str, str]]:
    """OpenAI/ElevenLabs 공통으로 전달할 수 있는 ISO 639-1 전사 언어 목록."""
    return [
        ("af", "Afrikaans"), ("ar", "Arabic"), ("hy", "Armenian"), ("az", "Azerbaijani"), ("be", "Belarusian"),
        ("bs", "Bosnian"), ("bg", "Bulgarian"), ("ca", "Catalan"), ("zh", "Chinese"), ("hr", "Croatian"),
        ("cs", "Czech"), ("da", "Danish"), ("nl", "Dutch"), ("en", "English"), ("et", "Estonian"),
        ("fi", "Finnish"), ("fr", "French"), ("gl", "Galician"), ("de", "German"), ("el", "Greek"),
        ("he", "Hebrew"), ("hi", "Hindi"), ("hu", "Hungarian"), ("id", "Indonesian"), ("it", "Italian"),
        ("ja", "Japanese"), ("kn", "Kannada"), ("kk", "Kazakh"), ("ko", "Korean"), ("lv", "Latvian"),
        ("lt", "Lithuanian"), ("mk", "Macedonian"), ("ms", "Malay"), ("mr", "Marathi"), ("mi", "Maori"),
        ("ne", "Nepali"), ("no", "Norwegian"), ("fa", "Persian"), ("pl", "Polish"), ("pt", "Portuguese"),
        ("ro", "Romanian"), ("ru", "Russian"), ("sr", "Serbian"), ("sk", "Slovak"), ("sl", "Slovenian"),
        ("es", "Spanish"), ("sw", "Swahili"), ("sv", "Swedish"), ("tl", "Tagalog"), ("ta", "Tamil"),
        ("th", "Thai"), ("tr", "Turkish"), ("uk", "Ukrainian"), ("ur", "Urdu"), ("vi", "Vietnamese"), ("cy", "Welsh"),
    ]


def supports_no_verbatim(model_id: str) -> bool:
    """현재 파일 전사 경로에서 no_verbatim을 받는 ElevenLabs 모델만 허용한다."""
    return model_id in {"scribe_v2", "scribe_v2_medical"}

# ------------------------------------------------------------------ #
# 접근성 권한 (macOS 전용)
# ------------------------------------------------------------------ #
def is_accessibility_granted() -> bool:
    if PLATFORM != "darwin":
        return True
    try:
        lib = ctypes.cdll.LoadLibrary(ctypes.util.find_library("ApplicationServices"))
        lib.AXIsProcessTrusted.restype = ctypes.c_bool
        return bool(lib.AXIsProcessTrusted())
    except Exception:
        log.exception("접근성 권한 확인 실패")
        return True


# ================================================================== #
# 오버레이 — macOS (AppKit/Quartz 기반, 볼륨 바 애니메이션 포함)
# ================================================================== #
if PLATFORM == "darwin":
    class RecordingOverlay:
        WIDTH = 230
        HEIGHT = 52
        BAR_COUNT = 5
        BAR_W = 3
        BAR_GAP = 4
        BAR_MAX_H = 28
        BAR_MIN_H = 4

        def __init__(self):
            self._available = False
            self._volume = 0.0
            self._phase = 0.0
            self._bar_layers = []
            self._bar_xs = []

            try:
                from AppKit import (
                    NSWindow, NSTextField, NSColor, NSFont, NSMakeRect,
                    NSBackingStoreBuffered, NSScreen,
                )
                try:
                    from AppKit import NSWindowStyleMaskBorderless as _borderless
                except ImportError:
                    try:
                        from AppKit import NSBorderlessWindowMask as _borderless
                    except ImportError:
                        _borderless = 0
                try:
                    from AppKit import NSWindowLevelFloating as _floating_level
                except ImportError:
                    try:
                        from AppKit import NSFloatingWindowLevel as _floating_level
                    except ImportError:
                        _floating_level = 3
                try:
                    from AppKit import NSTextAlignmentLeft as _align_left
                except ImportError:
                    _align_left = 0

                screen = NSScreen.mainScreen()
                vis = screen.visibleFrame()
                full = screen.frame()
                x = (full.size.width - self.WIDTH) / 2
                y = vis.origin.y + 40
                log.debug("오버레이 위치: (%.0f, %.0f)", x, y)

                self._win = NSWindow.alloc().initWithContentRect_styleMask_backing_defer_(
                    NSMakeRect(x, y, self.WIDTH, self.HEIGHT),
                    _borderless,
                    NSBackingStoreBuffered,
                    False,
                )
                self._win.setLevel_(_floating_level)
                self._win.setOpaque_(False)
                self._win.setBackgroundColor_(NSColor.clearColor())
                self._win.setHasShadow_(True)
                self._win.setIgnoresMouseEvents_(True)

                content = self._win.contentView()
                content.setWantsLayer_(True)
                content.layer().setBackgroundColor_(
                    NSColor.colorWithCalibratedRed_green_blue_alpha_(0.1, 0.1, 0.1, 0.88).CGColor()
                )
                content.layer().setCornerRadius_(self.HEIGHT / 2)
                content.layer().setMasksToBounds_(True)

                font_h = 20
                label_y = (self.HEIGHT - font_h) // 2
                label_x = 22
                label_w = 118
                self._label = NSTextField.alloc().initWithFrame_(
                    NSMakeRect(label_x, label_y, label_w, font_h)
                )
                self._label.setEditable_(False)
                self._label.setBezeled_(False)
                self._label.setDrawsBackground_(False)
                self._label.setTextColor_(NSColor.whiteColor())
                self._label.setAlignment_(_align_left)
                self._label.setFont_(NSFont.systemFontOfSize_(14))
                content.addSubview_(self._label)

                bar_color = NSColor.colorWithCalibratedRed_green_blue_alpha_(1.0, 0.28, 0.28, 1.0)
                bar_area_x = label_x + label_w + 6
                bar_total_w = self.BAR_COUNT * self.BAR_W + (self.BAR_COUNT - 1) * self.BAR_GAP
                avail_w = self.WIDTH - bar_area_x - 22
                bar_start_x = bar_area_x + (avail_w - bar_total_w) / 2

                try:
                    from Quartz import CALayer
                    for i in range(self.BAR_COUNT):
                        bx = bar_start_x + i * (self.BAR_W + self.BAR_GAP)
                        layer = CALayer.layer()
                        layer.setFrame_(NSMakeRect(bx, (self.HEIGHT - self.BAR_MIN_H) / 2, self.BAR_W, self.BAR_MIN_H))
                        layer.setBackgroundColor_(bar_color.CGColor())
                        layer.setCornerRadius_(self.BAR_W / 2)
                        content.layer().addSublayer_(layer)
                        self._bar_layers.append(layer)
                        self._bar_xs.append(bx)
                    log.info("오버레이 볼륨 바 초기화 OK")
                except Exception:
                    log.exception("오버레이 볼륨 바 초기화 실패")

                self._available = True
                log.info("macOS 오버레이 초기화 완료")
            except Exception:
                log.exception("macOS 오버레이 초기화 실패")

        def set_volume(self, level: float):
            self._volume = max(0.0, min(1.0, level))

        def tick(self):
            if not self._available or not self._bar_layers:
                return
            self._phase += 0.4
            self._volume *= 0.88
            v = self._volume
            try:
                from Quartz import CATransaction
                from AppKit import NSMakeRect
                CATransaction.begin()
                CATransaction.setDisableActions_(True)
                for i, layer in enumerate(self._bar_layers):
                    wave = 0.5 + 0.5 * math.sin(self._phase + i * 1.3)
                    lv = v * (0.5 + 0.5 * wave) + (1 - v) * 0.12 * wave
                    h = self.BAR_MIN_H + lv * (self.BAR_MAX_H - self.BAR_MIN_H)
                    layer.setFrame_(NSMakeRect(self._bar_xs[i], (self.HEIGHT - h) / 2, self.BAR_W, h))
                CATransaction.commit()
            except Exception:
                log.exception("오버레이 tick 오류")

        def show(self, text: str = "🔴  Recording"):
            if self._available:
                from AppKit import NSScreen, NSMakeRect
                screen = NSScreen.mainScreen()
                full = screen.frame()
                vis = screen.visibleFrame()
                x = (full.size.width - self.WIDTH) / 2
                y = vis.origin.y + 40
                log.debug("오버레이 표시: '%s'", text)
                self._label.setStringValue_(text)
                cur = self._win.frame()
                self._win.setFrame_display_(NSMakeRect(x, y, cur.size.width, cur.size.height), False)
                self._win.orderFrontRegardless()

        def hide(self):
            if self._available:
                log.debug("오버레이 숨김")
                self._volume = 0.0
                self._win.orderOut_(None)


# ================================================================== #
# 오버레이 — Windows (tkinter 기반)
# ================================================================== #
else:
    class RecordingOverlay:
        WIDTH = 230
        HEIGHT = 52
        BAR_COUNT = 5
        BAR_W = 3
        BAR_GAP = 4
        BAR_MAX_H = 28
        BAR_MIN_H = 4

        def __init__(self, root):
            self._available = False
            self._volume = 0.0
            self._phase = 0.0
            self._bar_ids: list[int] = []
            self._bar_xs: list[float] = []
            self._canvas = None
            self._text_id = None

            try:
                self._win = tk.Toplevel(root)
                self._win.withdraw()
                self._win.overrideredirect(True)
                self._win.attributes('-topmost', True)
                self._win.attributes('-alpha', 0.88)
                self._win.configure(bg='#1a1a1a')

                sw = root.winfo_screenwidth()
                sh = root.winfo_screenheight()
                x = (sw - self.WIDTH) // 2
                y = sh - self.HEIGHT - 60
                self._win.geometry(f"{self.WIDTH}x{self.HEIGHT}+{x}+{y}")

                self._canvas = tk.Canvas(
                    self._win, width=self.WIDTH, height=self.HEIGHT,
                    bg='#1a1a1a', highlightthickness=0,
                )
                self._canvas.pack(fill='both', expand=True)

                label_x = 22
                label_w = 118
                self._text_id = self._canvas.create_text(
                    label_x, self.HEIGHT // 2,
                    text="", fill='white', font=('Segoe UI', 12), anchor='w',
                )

                bar_area_x = label_x + label_w + 6
                bar_total_w = self.BAR_COUNT * self.BAR_W + (self.BAR_COUNT - 1) * self.BAR_GAP
                avail_w = self.WIDTH - bar_area_x - 22
                bar_start_x = bar_area_x + (avail_w - bar_total_w) / 2
                mid_y = self.HEIGHT / 2

                for i in range(self.BAR_COUNT):
                    bx = bar_start_x + i * (self.BAR_W + self.BAR_GAP)
                    y0 = mid_y - self.BAR_MIN_H / 2
                    y1 = mid_y + self.BAR_MIN_H / 2
                    bid = self._canvas.create_rectangle(
                        bx, y0, bx + self.BAR_W, y1,
                        fill='#ff4747', outline='',
                    )
                    self._bar_ids.append(bid)
                    self._bar_xs.append(bx)

                self._available = True
                log.info("Windows 오버레이 초기화 완료 (볼륨 바 포함)")
            except Exception:
                log.exception("Windows 오버레이 초기화 실패")

        def set_volume(self, level: float):
            self._volume = max(0.0, min(1.0, level))

        def tick(self):
            if not self._available or not self._bar_ids:
                return
            self._phase += 0.4
            self._volume *= 0.88
            v = self._volume
            try:
                mid_y = self.HEIGHT / 2
                for i, bid in enumerate(self._bar_ids):
                    wave = 0.5 + 0.5 * math.sin(self._phase + i * 1.3)
                    lv = v * (0.5 + 0.5 * wave) + (1 - v) * 0.12 * wave
                    h = self.BAR_MIN_H + lv * (self.BAR_MAX_H - self.BAR_MIN_H)
                    bx = self._bar_xs[i]
                    y0 = mid_y - h / 2
                    y1 = mid_y + h / 2
                    self._canvas.coords(bid, bx, y0, bx + self.BAR_W, y1)
            except Exception:
                log.exception("오버레이 tick 오류")

        def show(self, text: str = "🔴  Recording"):
            if self._available:
                log.debug("오버레이 표시: '%s'", text)
                self._canvas.itemconfigure(self._text_id, text=text)
                self._win.deiconify()

        def hide(self):
            if self._available:
                log.debug("오버레이 숨김")
                self._volume = 0.0
                self._win.withdraw()


# ================================================================== #
# 공통 로직 Mixin
# ================================================================== #
class VoiceSTTCore:
    """
    플랫폼 독립적인 녹음·STT·키보드 로직.

    서브클래스가 구현해야 하는 메서드:
      _set_tray_title(title)  — 트레이 아이콘/타이틀 변경
      _set_status(status)     — 메뉴의 상태 텍스트 변경
      _set_last(text)         — 마지막 변환 결과 텍스트 변경
    """
    SAMPLE_RATE = 16000

    def _core_init(self):
        self.recording = False
        self._transcribing = False
        self._cancelled = False
        self._reset_timer: threading.Timer | None = None
        self.audio_frames: list[np.ndarray] = []
        self.stream: sd.InputStream | None = None
        self._prev_muted: bool | None = None
        self._cfg_trigger_key = keyboard.Key.alt_r
        self._cfg_recording_control = "hold"
        self._trigger_key_down = False
        self.overlay = None
        self._ui_queue: queue.Queue = queue.Queue()
        log.debug("코어 상태 변수 초기화 완료")

        self._reload_shortcut_cache()

        log.info("키보드 리스너 시작 중...")
        listener_options = {}
        # 후크 콜백에서 마이크를 열면 장치 초기화 중 키 입력 처리가 지연된다.
        # 모든 플랫폼에서 이벤트만 큐에 넣고 녹음 처리는 별도 스레드에서 한다.
        self._key_events = queue.SimpleQueue()
        threading.Thread(target=self._process_key_events, daemon=True,
                         name="keyscribe-key-events").start()
        if PLATFORM == "darwin":
            # 수동 감시 탭은 입력 모니터링 권한이 필요하다. 이벤트를 그대로
            # 통과시키는 활성 탭은 이미 요청하는 손쉬운 사용 권한을 사용한다.
            listener_options["darwin_intercept"] = lambda _event_type, event: event
        listener = keyboard.Listener(
            on_press=self._queue_key_press,
            on_release=self._queue_key_release,
            **listener_options,
        )
        listener.daemon = True
        threading.Thread(target=listener.start, daemon=True).start()
        log.info("키보드 리스너 스레드 시작됨")

    # ------------------------------------------------------------------
    # 단축키 파싱 및 캐시
    # ------------------------------------------------------------------

    @staticmethod
    def _key_from_str(key_str: str):
        _MAP = {
            "right_option": keyboard.Key.alt_r,
            "left_option":  keyboard.Key.alt,
            "option":       keyboard.Key.alt,
            "right_alt":    keyboard.Key.alt_r,
            "left_alt":     keyboard.Key.alt,
            "alt":          keyboard.Key.alt,
            "right_ctrl":   keyboard.Key.ctrl_r,
            "left_ctrl":    keyboard.Key.ctrl,
            "ctrl":         keyboard.Key.ctrl,
            "right_shift":  keyboard.Key.shift_r,
            "left_shift":   keyboard.Key.shift,
            "shift":        keyboard.Key.shift,
            "right_cmd":    keyboard.Key.cmd_r,
            "left_cmd":     keyboard.Key.cmd,
            "cmd":          keyboard.Key.cmd,
            "enter":        keyboard.Key.enter,
            "esc":          keyboard.Key.esc,
            "tab":          keyboard.Key.tab,
            "space":        keyboard.Key.space,
            "f1":  keyboard.Key.f1,  "f2":  keyboard.Key.f2,
            "f3":  keyboard.Key.f3,  "f4":  keyboard.Key.f4,
            "f5":  keyboard.Key.f5,  "f6":  keyboard.Key.f6,
            "f7":  keyboard.Key.f7,  "f8":  keyboard.Key.f8,
            "f9":  keyboard.Key.f9,  "f10": keyboard.Key.f10,
            "f11": keyboard.Key.f11, "f12": keyboard.Key.f12,
        }
        if key_str in _MAP:
            return _MAP[key_str]
        if len(key_str) == 1:
            return keyboard.KeyCode.from_char(key_str)
        log.warning("알 수 없는 키 문자열 '%s'", key_str)
        return None

    def _reload_shortcut_cache(self):
        try:
            config = load_config()
            shortcut = config.get("shortcut", "")
            k = self._key_from_str(shortcut) if shortcut else None
            self._cfg_trigger_key = k if k else keyboard.Key.alt_r
            control = config.get("recording_control", "hold")
            self._cfg_recording_control = control if control in ("hold", "toggle") else "hold"
            self._trigger_key_down = False
            log.debug("단축키 캐시 갱신 — trigger=%r, control=%s",
                      self._cfg_trigger_key, self._cfg_recording_control)
        except Exception:
            log.exception("단축키 캐시 갱신 실패 — 기존 값 유지")

    # ------------------------------------------------------------------
    # UI 큐
    # ------------------------------------------------------------------

    def _ui(self, fn):
        self._ui_queue.put(fn)

    def _flush_ui_queue_impl(self):
        processed = 0
        while True:
            try:
                fn = self._ui_queue.get_nowait()
            except queue.Empty:
                break
            try:
                fn()
                processed += 1
            except Exception:
                log.exception("UI 큐 콜백 오류")
        if processed > 0:
            log.debug("UI 큐 처리 완료 — %d개", processed)
        if self.overlay and (self.recording or self._transcribing):
            self.overlay.tick()

    # ------------------------------------------------------------------
    # 키 이벤트
    # ------------------------------------------------------------------

    def _queue_key_press(self, key):
        if key == self._cfg_trigger_key or key == keyboard.Key.esc:
            self._key_events.put(("press", key))

    def _queue_key_release(self, key):
        if key == self._cfg_trigger_key:
            self._key_events.put(("release", key))

    def _process_key_events(self):
        while True:
            event_type, key = self._key_events.get()
            try:
                if event_type == "press":
                    self._on_press(key)
                else:
                    self._on_release(key)
            except Exception:
                log.exception("키 이벤트 처리 실패 — type=%s, key=%r", event_type, key)

    def _on_press(self, key):
        log.debug("키 눌림: %r", key)
        if key == keyboard.Key.esc:
            if self.recording or self._transcribing:
                log.info("ESC — 녹음 취소")
                self._cancel_recording()
            return

        if key == self._cfg_trigger_key:
            if self._trigger_key_down:
                log.debug("트리거 키 반복 눌림 — 무시")
                return
            self._trigger_key_down = True
            if self.recording:
                if self._cfg_recording_control == "toggle":
                    log.info("트리거 키 다시 눌림 — STT 변환 시작")
                    self._stop_and_transcribe()
                else:
                    log.debug("트리거 키 눌림 — 이미 녹음중, 무시")
            elif self._transcribing:
                log.debug("트리거 키 눌림 — 변환중, 무시")
            else:
                log.info("트리거 키 눌림 — 녹음 시작")
                self._start_recording()

    def _on_release(self, key):
        log.debug("키 뗌: %r", key)
        if key == self._cfg_trigger_key:
            self._trigger_key_down = False
            if self.recording and self._cfg_recording_control == "hold":
                log.info("트리거 키 뗌 — STT 변환 시작")
                self._stop_and_transcribe()
            else:
                log.debug("트리거 키 뗌 — 녹음중 아님 (transcribing=%s)", self._transcribing)

    # ------------------------------------------------------------------
    # 마이크 스트림 생성 (PortAudio 캐시 자동 복구)
    # ------------------------------------------------------------------

    def _create_input_stream(self, **kwargs) -> "sd.InputStream":
        """
        sd.InputStream 생성. macOS CoreAudio는 PortAudio 초기화 시점에
        디바이스 목록/기본 입력장치를 캐시해두고, 앱 실행 중 시스템 설정에서
        입력장치를 바꿔도 이 캐시를 자동으로 갱신하지 않는다 — 그대로 두면
        에러 없이 예전(또는 사라진) 장치로 스트림이 열려 무음만 녹음된다.
        매번 스트림을 열기 전 PortAudio를 재초기화해 최신 장치 상태를 강제로
        다시 읽는다 (재초기화 자체는 수 ms 수준이라 지연은 무시할 만하다).
        """
        try:
            sd._terminate()
            sd._initialize()
        except Exception:
            log.exception("PortAudio 사전 재초기화 실패 — 기존 상태로 계속 진행")
        try:
            return sd.InputStream(**kwargs)
        except sd.PortAudioError as e:
            log.warning("InputStream 생성 실패 — PortAudio 재초기화 후 재시도: %s", e)
            try:
                sd._terminate()
                sd._initialize()
                log.info("PortAudio 재초기화 완료")
            except Exception:
                log.exception("PortAudio 재초기화 실패")
            return sd.InputStream(**kwargs)

    # ------------------------------------------------------------------
    # 시스템 오디오 음소거
    # ------------------------------------------------------------------

    def _mute_system_audio(self):
        config = load_config()
        if not config.get("mute_during_recording", True):
            return
        if PLATFORM == "darwin":
            try:
                result = subprocess.run(
                    ["osascript", "-e", "output muted of (get volume settings)"],
                    capture_output=True, text=True, timeout=1.0,
                )
                self._prev_muted = result.stdout.strip() == "true"
                if not self._prev_muted:
                    subprocess.run(
                        ["osascript", "-e", "set volume output muted true"],
                        timeout=1.0,
                    )
                    log.info("시스템 오디오 음소거 설정")
                else:
                    log.debug("시스템 오디오 이미 음소거 상태")
            except Exception:
                log.exception("시스템 오디오 음소거 실패")
                self._prev_muted = None
        # Windows: 현재 음소거 미지원

    def _restore_system_audio(self):
        if self._prev_muted is None:
            return
        if PLATFORM == "darwin" and not self._prev_muted:
            try:
                subprocess.run(
                    ["osascript", "-e", "set volume output muted false"],
                    timeout=1.0,
                )
                log.info("시스템 오디오 음소거 해제")
            except Exception:
                log.exception("시스템 오디오 음소거 해제 실패")
        self._prev_muted = None

    # ------------------------------------------------------------------
    # 붙여넣기
    # ------------------------------------------------------------------

    def _paste_text(self, text: str):
        config = load_config()
        # auto_send이 없던 기존 설정은 ptt_after_key=enter만 자동 전송으로 해석한다.
        auto_send = bool(config.get("auto_send", config.get("ptt_after_key", "enter") == "enter"))
        after_key = keyboard.Key.enter if auto_send else None
        log.debug("_paste_text — after=%r, %d자", after_key, len(text))
        kb = keyboard.Controller()

        if text:
            if PLATFORM == "win32":
                kb.type(text)
                log.info("타이핑 입력 완료 (%d자)", len(text))
            else:
                pyperclip.copy(text)
                with kb.pressed(keyboard.Key.cmd):
                    kb.press("v")
                    kb.release("v")
            time.sleep(0.1)

        if after_key:
            kb.press(after_key)
            kb.release(after_key)
            time.sleep(0.05)
        log.info("텍스트 붙여넣기 완료")

    # ------------------------------------------------------------------
    # 텍스트 정제
    # ------------------------------------------------------------------

    @staticmethod
    def _clean_text(text: str) -> str:
        text = re.sub(r'\[.*?\]', '', text)
        return ' '.join(text.split())

    # ------------------------------------------------------------------
    # 녹음
    # ------------------------------------------------------------------

    def _cancel_recording(self):
        log.debug("_cancel_recording — recording=%s, transcribing=%s", self.recording, self._transcribing)
        self._cancelled = True
        self.recording = False
        self._restore_system_audio()
        if self.stream:
            try:
                self.stream.stop()
                self.stream.close()
            except Exception:
                log.exception("마이크 스트림 중지 실패")
            self.stream = None
        log.info("녹음 취소됨")
        self._ui(lambda: (
            self._set_tray_title("🎙"),
            self._set_status(tr("status_cancelled")),
            self.overlay.hide() if self.overlay else None,
        ))
        if self._reset_timer:
            self._reset_timer.cancel()

        def _on_cancel():
            self._ui(lambda: self._set_status(tr("status_idle")))

        self._reset_timer = threading.Timer(2.5, _on_cancel)
        self._reset_timer.start()

    def _start_recording(self):
        log.debug("_start_recording")
        if self._reset_timer:
            self._reset_timer.cancel()
            self._reset_timer = None
        self._cancelled = False
        self.recording = True
        self.audio_frames = []
        self._mute_system_audio()
        self._ui(lambda: (
            self._set_tray_title("🔴"),
            self._set_status(tr("status_recording")),
            self.overlay.show(tr("overlay_recording")) if self.overlay else None,
        ))
        try:
            log.debug("마이크 스트림 생성 — samplerate=%d", self.SAMPLE_RATE)
            self.stream = self._create_input_stream(
                samplerate=self.SAMPLE_RATE,
                channels=1,
                dtype="int16",
                callback=self._audio_callback,
            )
            self.stream.start()
            log.info("마이크 스트림 시작 완료")
        except Exception:
            log.exception("마이크 스트림 실패")
            if self.stream:
                try:
                    self.stream.close()
                except Exception:
                    pass
            self.stream = None
            self.recording = False
            self._ui(lambda: (
                self._set_tray_title("🎙"),
                self._set_status(tr("status_mic_error")),
                self.overlay.show(tr("overlay_no_mic")) if self.overlay else None,
            ))
            if PLATFORM == "win32":
                self._ui(self._show_microphone_help)
            if self._reset_timer:
                self._reset_timer.cancel()

            def _on_mic_error_reset():
                self._ui(lambda: (
                    self._set_status(tr("status_idle")),
                    self.overlay.hide() if self.overlay else None,
                ))

            self._reset_timer = threading.Timer(2.5, _on_mic_error_reset)
            self._reset_timer.start()

    def _audio_callback(self, indata, frames, time_info, status):
        if status:
            log.warning("오디오 콜백 상태: %s", status)
        self.audio_frames.append(indata.copy())
        rms = float(np.sqrt(np.mean(indata.astype(np.float32) ** 2)))
        if self.overlay:
            self.overlay.set_volume(min(1.0, rms / 4000.0))

    def _stop_and_transcribe(self):
        log.debug("_stop_and_transcribe — frames=%d", len(self.audio_frames))
        self.recording = False
        self._restore_system_audio()
        self._ui(lambda: (
            self._set_tray_title("⏳"),
            self._set_status(tr("status_transcribing")),
            self.overlay.show(tr("overlay_transcribing")) if self.overlay else None,
        ))
        if self.stream:
            try:
                self.stream.stop()
                self.stream.close()
            except Exception:
                log.exception("마이크 스트림 중지 실패")
            self.stream = None
        log.info("녹음 종료 — %d 청크", len(self.audio_frames))
        if self._cancelled:
            return
        self._transcribing = True
        threading.Thread(target=self._transcribe, daemon=True).start()

    # ------------------------------------------------------------------
    # STT
    # ------------------------------------------------------------------

    def _call_stt(self, config: dict, api_key: str, tmp_path: str,
                  hard_timeout: float, sdk_timeout: float) -> object:
        """API 키 prefix로 STT 제공자 라우팅. sk- → OpenAI gpt-transcribe, 그 외 → ElevenLabs."""
        if api_key.startswith("sk-"):
            return self._call_openai(config, api_key, tmp_path, hard_timeout, sdk_timeout)
        return self._call_elevenlabs(config, api_key, tmp_path, hard_timeout, sdk_timeout)

    def _call_elevenlabs(self, config: dict, api_key: str, tmp_path: str,
                         hard_timeout: float, sdk_timeout: float) -> object:
        last_exc = None
        result = None
        for attempt in range(1, 3):
            if self._cancelled:
                return None
            try:
                log.info("ElevenLabs API 호출 (시도 %d/2)", attempt)
                client = ElevenLabs(api_key=api_key, timeout=sdk_timeout)
                _holder: dict = {}

                def _call(_holder=_holder):
                    try:
                        with open(tmp_path, "rb") as f:
                            model_id = config.get("elevenlabs_model", "scribe_v2")
                            request = {
                                "file": f,
                                "model_id": model_id,
                                "language_code": config.get("language", "ko"),
                                "keyterms": config.get("keyterms", []) or None,
                            }
                            if supports_no_verbatim(model_id):
                                request["no_verbatim"] = config.get("no_verbatim", True)
                            _holder["result"] = client.speech_to_text.convert(**request)
                    except BaseException as exc:
                        _holder["error"] = exc

                _t = threading.Thread(target=_call, daemon=True)
                _t.start()
                _t.join(timeout=hard_timeout)

                if _t.is_alive():
                    raise TimeoutError(f"ElevenLabs API 응답 없음 ({hard_timeout:.0f}초 초과)")
                if "error" in _holder:
                    raise _holder["error"]

                result = _holder["result"]
                last_exc = None
                log.info("API 응답 수신 완료 (시도 %d)", attempt)
                break

            except Exception as e:
                last_exc = e
                log.warning("API 오류 (시도 %d): [%s] %s", attempt, type(e).__name__, e)
                if attempt == 1 and not self._cancelled:
                    self._ui(lambda: (
                        self._set_status(tr("status_retrying")),
                        self.overlay.show(tr("overlay_retrying")) if self.overlay else None,
                    ))
                    time.sleep(1.0)

        if last_exc is not None:
            raise last_exc
        return result

    def _call_openai(self, config: dict, api_key: str, tmp_path: str,
                     hard_timeout: float, sdk_timeout: float) -> object:
        if httpx is None:
            raise RuntimeError("httpx 모듈이 로드되지 않아 OpenAI 호출 불가")

        model = config.get("openai_model", "gpt-transcribe")
        language = config.get("language", "ko")
        keyterms = config.get("keyterms", []) or []
        # 용어는 자연어 문장으로 제공해 전사 정확도를 돕는다.
        prompt = f"이 녹음에는 다음 용어가 포함됩니다: {', '.join(keyterms)}." if keyterms else None

        last_exc = None
        result = None
        for attempt in range(1, 3):
            if self._cancelled:
                return None
            try:
                log.info("OpenAI Transcription API 호출 (시도 %d/2) — model=%s", attempt, model)
                _holder: dict = {}

                def _call(_holder=_holder):
                    try:
                        with open(tmp_path, "rb") as f:
                            files = {"file": (os.path.basename(tmp_path), f, "audio/flac")}
                            data = {"model": model, "language": language}
                            if prompt:
                                data["prompt"] = prompt
                            headers = {"Authorization": f"Bearer {api_key}"}
                            resp = httpx.post(
                                "https://api.openai.com/v1/audio/transcriptions",
                                headers=headers,
                                files=files,
                                data=data,
                                timeout=sdk_timeout,
                            )
                            resp.raise_for_status()
                            _holder["result"] = _STTResult(resp.json().get("text", ""))
                    except BaseException as exc:
                        _holder["error"] = exc

                _t = threading.Thread(target=_call, daemon=True)
                _t.start()
                _t.join(timeout=hard_timeout)

                if _t.is_alive():
                    raise TimeoutError(f"OpenAI API 응답 없음 ({hard_timeout:.0f}초 초과)")
                if "error" in _holder:
                    raise _holder["error"]

                result = _holder["result"]
                last_exc = None
                log.info("API 응답 수신 완료 (시도 %d)", attempt)
                break

            except Exception as e:
                last_exc = e
                log.warning("API 오류 (시도 %d): [%s] %s", attempt, type(e).__name__, e)
                if attempt == 1 and not self._cancelled:
                    self._ui(lambda: (
                        self._set_status(tr("status_retrying")),
                        self.overlay.show(tr("overlay_retrying")) if self.overlay else None,
                    ))
                    time.sleep(1.0)

        if last_exc is not None:
            raise last_exc
        return result

    def _transcribe(self):
        thread_id = threading.current_thread().ident
        log.debug("_transcribe 진입 — thread_id=%d", thread_id)
        t_start = time.time()
        tmp_path = None
        error_occurred = False
        try:
            config = load_config()
            api_key = self._api_key
            if not api_key:
                raise ValueError("API Key가 설정되지 않았습니다.")

            if self._cancelled:
                return
            if not self.audio_frames:
                raise ValueError("녹음된 오디오 데이터가 없습니다.")

            audio_data = np.concatenate(self.audio_frames, axis=0)
            duration_sec = len(audio_data) / self.SAMPLE_RATE
            log.info("STT 변환 시작 — %.1f초 (%d 청크)", duration_sec, len(self.audio_frames))

            with tempfile.NamedTemporaryFile(suffix=".flac", delete=False) as f:
                tmp_path = f.name
            sf.write(tmp_path, audio_data, self.SAMPLE_RATE, format="FLAC", subtype="PCM_16")
            log.debug("FLAC 저장 완료 — %d bytes", os.path.getsize(tmp_path))

            hard_timeout = max(5.0, duration_sec + 10.0)
            sdk_timeout = hard_timeout - 2.0

            result = self._call_stt(config, api_key, tmp_path, hard_timeout, sdk_timeout)

            if self._cancelled:
                return

            text = self._clean_text(result.text or "")
            log.info("변환 결과: %d자 — %r", len(text), text[:80])

            if text:
                preview = text[:40] + ("..." if len(text) > 40 else "")
                self._ui(lambda t=text, p=preview: (
                    self._paste_text(t),
                    self._set_last(tr("last", text=p)),
                    self._set_status(tr("status_idle")),
                ))
            else:
                log.warning("변환 결과 텍스트 없음")
                self._ui(lambda: self._set_status(tr("status_no_text")))

        except Exception as e:
            error_occurred = True
            log.exception("STT 오류: [%s] %s", type(e).__name__, e)
            if not self._cancelled:
                self._ui(lambda: (
                    self._set_status(tr("status_failed")),
                    self.overlay.show(tr("overlay_failed")) if self.overlay else None,
                ))
                if self._reset_timer:
                    self._reset_timer.cancel()

                def _on_error():
                    self._ui(lambda: (
                        self._set_status(tr("status_idle")),
                        self.overlay.hide() if self.overlay else None,
                    ))

                self._reset_timer = threading.Timer(2.5, _on_error)
                self._reset_timer.start()

        finally:
            elapsed = time.time() - t_start
            log.debug("_transcribe 완료 — thread_id=%d, 소요=%.2f초", thread_id, elapsed)
            self._transcribing = False
            if tmp_path and os.path.exists(tmp_path):
                try:
                    os.unlink(tmp_path)
                except Exception:
                    log.exception("임시 파일 삭제 실패: %s", tmp_path)
            if not self._cancelled:
                self._ui(lambda: self._set_tray_title("🎙"))
                if not error_occurred:
                    self._ui(lambda: self.overlay.hide() if self.overlay else None)

    # ------------------------------------------------------------------
# macOS 앱
# ================================================================== #
if PLATFORM == "darwin":
    class VoiceSTTApp(rumps.App, VoiceSTTCore):
        def __init__(self):
            log.info("VoiceSTTApp (macOS) __init__ 시작")
            sync_ui_language()
            rumps.App.__init__(self, APP_NAME, title=None, icon=str(MENU_ICON_PATH),
                               template=True, quit_button=tr("quit"))

            self.status_item = rumps.MenuItem(tr("status_idle"))
            self.last_item = rumps.MenuItem(tr("last", text="-"))
            self.config_item = rumps.MenuItem(tr("settings"), callback=self._on_edit_config)
            self.restart_item = rumps.MenuItem(tr("restart"), callback=self._restart)
            log.debug("메뉴 아이템 생성 완료")

            user_cfg = load_user_config()
            api_key_source = (
                "user_config" if user_cfg.get("api_key")
                else "환경변수" if os.environ.get("ELEVENLABS_API_KEY")
                else "없음"
            )
            self._api_key: str = (
                user_cfg.get("api_key")
                or os.environ.get("ELEVENLABS_API_KEY")
                or ""
            )
            log.info("API 키: %s (소스: %s)", "설정됨" if self._api_key else "미설정", api_key_source)

            self._accessibility_item = rumps.MenuItem(
                tr("accessibility_required"), callback=self._open_accessibility_prefs,
            )
            self.menu = [self.status_item, self.last_item, None, self._accessibility_item,
                         self.config_item, None, self.restart_item, None]
            self._accessibility_granted = None
            self._sync_accessibility_warning()

            self._core_init()
            log.info("VoiceSTTApp (macOS) __init__ 완료")

        # ── 추상 메서드 구현 ──

        def _set_tray_title(self, title: str):
            # 메뉴바는 이모지 대신 앱 아이콘과 같은 벡터 마이크를 항상 표시한다.
            self.title = None

        def _set_status(self, status: str):
            self.status_item.title = status

        def _set_last(self, text: str):
            self.last_item.title = text

        # ── rumps 타이머 ──

        @rumps.timer(0.3)
        def _init_overlay_once(self, sender):
            sender.stop()
            log.info("오버레이 초기화 시작")
            self.overlay = RecordingOverlay()
            log.info("오버레이 초기화 완료 — available=%s", self.overlay._available)

        @rumps.timer(0.05)
        def _flush_ui_queue(self, _):
            self._flush_ui_queue_impl()

        @rumps.timer(2)
        def _check_accessibility(self, _):
            self._sync_accessibility_warning()

        @rumps.timer(0.7)
        def _prompt_for_accessibility_once(self, sender):
            sender.stop()
            if self._accessibility_granted:
                return
            try:
                from ApplicationServices import AXIsProcessTrustedWithOptions, kAXTrustedCheckOptionPrompt
                from Foundation import NSDictionary
                options = NSDictionary.dictionaryWithObject_forKey_(True, kAXTrustedCheckOptionPrompt)
                AXIsProcessTrustedWithOptions(options)
                log.info("접근성 권한 요청 대화상자 표시")
            except Exception:
                log.exception("접근성 권한 요청 대화상자 표시 실패")

        def _sync_accessibility_warning(self):
            granted = is_accessibility_granted()
            if granted == self._accessibility_granted:
                return
            previous = self._accessibility_granted
            log.info("접근성 권한 변경: %s → %s", previous, granted)
            self._accessibility_granted = granted
            self._accessibility_item._menuitem.setHidden_(granted)
            if granted and previous is False:
                log.info("권한 허용됨 — 키보드 리스너 재초기화를 위해 앱 재시작")
                self._restart(None)

        # ── 접근성 권한 ──

        def _open_accessibility_prefs(self, _):
            log.info("접근성 권한 설정 열기")
            subprocess.Popen([
                "open",
                "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility",
            ])
            rumps.notification(APP_NAME, "", tr("permission_restart"))

        # ── 재실행 ──

        def _restart(self, _):
            exe = sys.executable
            log.info("재실행 요청 — exe: %s", exe)
            if ".app/Contents" in exe:
                bundle = exe[:exe.index(".app/Contents") + 4]
                subprocess.Popen(["open", bundle])
            else:
                command = [sys.executable] + sys.argv
                log.info("재실행 명령: %r (cwd=%s)", command, CONFIG_PATH.parent)
                subprocess.Popen(command, cwd=CONFIG_PATH.parent,
                                 stdout=_log_handler.stream, stderr=_log_handler.stream)
            log.info("재실행 프로세스 시작 — 현재 앱 종료")
            rumps.quit_application()

        def _on_edit_config(self, _):
            from AppKit import (
                NSAlert, NSButton, NSColor, NSFont, NSMakeRect, NSPasteboard, NSPopUpButton, NSScrollView, NSTextField, NSTextView,
            )
            values = settings_values(self._api_key)
            displayed_api_key = abbreviate_api_key(self._api_key)
            W, H, row_h = 520, 456, 46
            view = __import__("AppKit").NSView.alloc().initWithFrame_(NSMakeRect(0, 0, W, H))
            fields = {}
            labels = [("api_key", tr("api_key"))]
            model_picker = None
            y = H - 40
            for key, label in labels:
                title = NSTextField.labelWithString_(label)
                title.setFrame_(NSMakeRect(0, y + 1, 130, 24))
                field = NSTextField.alloc().initWithFrame_(NSMakeRect(135, y, 375, 24))
                field.setStringValue_(displayed_api_key if key == "api_key" else values[key])
                if key == "api_key":
                    field.setPlaceholderString_("sk_… (ElevenLabs) 또는 sk-… (OpenAI)")
                    field.setDelegate_(self)
                view.addSubview_(title)
                view.addSubview_(field)
                fields[key] = field
                if key == "api_key":
                    hint = NSTextField.labelWithString_("(ElevenLabs / OpenAI)")
                    hint.setFrame_(NSMakeRect(135, y - 14, 220, 16))
                    hint.setFont_(NSFont.systemFontOfSize_(10))
                    hint.setTextColor_(NSColor.secondaryLabelColor())
                    view.addSubview_(hint)
                    model_title = NSTextField.labelWithString_(tr("model"))
                    model_title.setFrame_(NSMakeRect(0, y - row_h + 1, 130, 24))
                    model_picker = NSPopUpButton.alloc().initWithFrame_pullsDown_(
                        NSMakeRect(135, y - row_h, 280, 26), False)
                    refresh_button = NSButton.alloc().initWithFrame_(
                        NSMakeRect(422, y - row_h, 88, 26))
                    refresh_button.setTitle_(tr("refresh"))
                    refresh_button.setTarget_(self)
                    refresh_button.setAction_("refreshModels:")
                    model_picker.setTarget_(self)
                    model_picker.setAction_("modelSelectionChanged:")
                    try:
                        models = fetch_transcription_models(self._api_key)
                    except Exception:
                        log.exception("모델 목록 로드 실패")
                        models = []
                    models = models or [values["model"]]
                    if values["model"] not in models:
                        models.insert(0, values["model"])
                    model_picker.addItemsWithTitles_(models)
                    model_picker.selectItemWithTitle_(values["model"])
                    self._model_api_key_field = field
                    self._model_picker = model_picker
                    self._api_pasteboard_change_count = NSPasteboard.generalPasteboard().changeCount()
                    view.addSubview_(model_title)
                    view.addSubview_(refresh_button)
                    view.addSubview_(model_picker)
                    y -= row_h
                y -= row_h

            def add_picker(title_text, items, value):
                nonlocal y
                items = list(items)
                if value not in [code for code, _ in items]:
                    items.insert(0, (value, value))
                title = NSTextField.labelWithString_(title_text)
                title.setFrame_(NSMakeRect(0, y + 1, 130, 24))
                picker = NSPopUpButton.alloc().initWithFrame_pullsDown_(NSMakeRect(135, y, 375, 26), False)
                codes = [code for code, _ in items]
                picker.addItemsWithTitles_([name for _, name in items])
                picker.selectItemAtIndex_(codes.index(value) if value in codes else 0)
                view.addSubview_(title)
                view.addSubview_(picker)
                y -= row_h
                return picker, codes

            speech_picker, speech_codes = add_picker(tr("speech_language"), speech_language_options(), values["language"])
            shortcut_title = NSTextField.labelWithString_(tr("shortcut"))
            shortcut_title.setFrame_(NSMakeRect(0, y + 1, 130, 24))
            shortcut_picker = NSPopUpButton.alloc().initWithFrame_pullsDown_(
                NSMakeRect(135, y, 375, 26), False)
            shortcut_items = shortcut_options()
            shortcut_values = [value for value, _ in shortcut_items]
            shortcut_picker.addItemsWithTitles_([label for _, label in shortcut_items])
            shortcut_picker.selectItemAtIndex_(shortcut_values.index(values["shortcut"])
                                               if values["shortcut"] in shortcut_values else 0)
            view.addSubview_(shortcut_title)
            view.addSubview_(shortcut_picker)
            y -= row_h

            control_picker, control_values = add_picker(
                tr("recording_control"), recording_control_options(), values["recording_control"])

            y -= row_h
            title = NSTextField.labelWithString_(tr("keywords"))
            title.setFrame_(NSMakeRect(0, y + 18, 130, 42))
            view.addSubview_(title)
            scroll = NSScrollView.alloc().initWithFrame_(NSMakeRect(135, y, 375, 84))
            scroll.setHasVerticalScroller_(True)
            keyterms = NSTextView.alloc().initWithFrame_(NSMakeRect(0, 0, 375, 84))
            keyterms.setString_(values["keyterms"])
            scroll.setDocumentView_(keyterms)
            view.addSubview_(scroll)
            # 키워드 입력칸과 첫 체크박스 사이에 12pt만 둔다.
            y -= 36
            no_verbatim = NSButton.alloc().initWithFrame_(NSMakeRect(135, y, 250, 24))
            no_verbatim.setButtonType_(3)
            no_verbatim.setTitle_(tr("remove_fillers"))
            no_verbatim.setState_(1 if values["no_verbatim"] else 0)
            no_verbatim.setEnabled_(provider_for_api_key(self._api_key) == "elevenlabs"
                                   and supports_no_verbatim(values["elevenlabs_model"]))
            view.addSubview_(no_verbatim)
            self._no_verbatim_button = no_verbatim
            filler_hint = NSTextField.labelWithString_(tr("filler_hint"))
            filler_hint.setFrame_(NSMakeRect(135, y - 15, 200, 16))
            filler_hint.setFont_(NSFont.systemFontOfSize_(10))
            filler_hint.setTextColor_(NSColor.secondaryLabelColor() if no_verbatim.isEnabled()
                                     else NSColor.disabledControlTextColor())
            view.addSubview_(filler_hint)
            self._no_verbatim_hint = filler_hint
            y -= 42
            mute = NSButton.alloc().initWithFrame_(NSMakeRect(135, y, 300, 24))
            mute.setButtonType_(3)
            mute.setTitle_(tr("mute_audio"))
            mute.setState_(1 if values["mute_during_recording"] else 0)
            view.addSubview_(mute)
            y -= 34
            auto_send = NSButton.alloc().initWithFrame_(NSMakeRect(135, y, 300, 24))
            auto_send.setButtonType_(3)
            auto_send.setTitle_(tr("auto_send"))
            auto_send.setState_(1 if values["auto_send"] else 0)
            view.addSubview_(auto_send)
            alert = NSAlert.alloc().init()
            alert.setMessageText_(tr("settings_title", app=APP_NAME))
            alert.setInformativeText_(tr("api_key_help"))
            alert.addButtonWithTitle_(tr("save"))
            alert.addButtonWithTitle_(tr("cancel"))
            alert.setAccessoryView_(view)
            alert.window().setInitialFirstResponder_(fields["api_key"])
            response = alert.runModal()
            if response == 1000:
                values.update({key: field.stringValue() for key, field in fields.items()})
                if values["api_key"].strip() == displayed_api_key:
                    values["api_key"] = self._api_key
                values["keyterms"] = keyterms.string()
                values["shortcut"] = shortcut_values[shortcut_picker.indexOfSelectedItem()]
                values["recording_control"] = control_values[control_picker.indexOfSelectedItem()]
                values["language"] = speech_codes[speech_picker.indexOfSelectedItem()]
                provider = provider_for_api_key(values["api_key"])
                values[f"{provider}_model"] = model_picker.titleOfSelectedItem() if provider else values["model"]
                values["auto_send"] = bool(auto_send.state())
                values["no_verbatim"] = bool(no_verbatim.state())
                values["mute_during_recording"] = bool(mute.state())
                save_settings(values)
                sync_ui_language(values["language"])
                self._api_key = values["api_key"].strip()
                self._reload_shortcut_cache()
                rumps.notification(APP_NAME, "", tr("saved"))

        def refreshModels_(self, _):
            """설정 창의 API Key로 전사 모델 목록을 다시 읽는다."""
            from AppKit import NSColor
            field = getattr(self, "_model_api_key_field", None)
            picker = getattr(self, "_model_picker", None)
            if not field or not picker:
                return
            api_key = field.stringValue().strip()
            if api_key == abbreviate_api_key(self._api_key):
                api_key = self._api_key
            try:
                models = fetch_transcription_models(api_key)
                if not models:
                    raise RuntimeError(tr("no_models"))
                picker.removeAllItems()
                picker.addItemsWithTitles_(models)
                picker.selectItemAtIndex_(0)
                field.setTextColor_(NSColor.labelColor())
                self.modelSelectionChanged_(picker)
            except Exception as exc:
                log.exception("모델 목록 새로고침 실패")
                field.setTextColor_(NSColor.systemRedColor())
                rumps.alert(title=tr("api_key_error_title"), message=tr("api_key_error_detail", error=exc))

        def modelSelectionChanged_(self, _):
            field = getattr(self, "_model_api_key_field", None)
            picker = getattr(self, "_model_picker", None)
            checkbox = getattr(self, "_no_verbatim_button", None)
            hint = getattr(self, "_no_verbatim_hint", None)
            if not field or not picker or not checkbox:
                return
            api_key = field.stringValue().strip()
            if api_key == abbreviate_api_key(self._api_key):
                api_key = self._api_key
            enabled = (provider_for_api_key(api_key) == "elevenlabs"
                       and supports_no_verbatim(picker.titleOfSelectedItem() or ""))
            checkbox.setEnabled_(enabled)
            if hint:
                from AppKit import NSColor
                hint.setTextColor_(NSColor.secondaryLabelColor() if enabled else NSColor.disabledControlTextColor())

        def controlTextDidEndEditing_(self, notification):
            """API Key 입력을 확정하거나 다른 곳으로 이동하면 모델을 자동 갱신한다."""
            field = getattr(self, "_model_api_key_field", None)
            if field and notification.object() == field:
                self.refreshModels_(None)

        def controlTextDidChange_(self, notification):
            """API Key를 붙여넣으면 포커스를 옮기지 않아도 모델을 갱신한다."""
            field = getattr(self, "_model_api_key_field", None)
            if not field or notification.object() != field:
                return
            from AppKit import NSPasteboard
            pasteboard_count = NSPasteboard.generalPasteboard().changeCount()
            if pasteboard_count != getattr(self, "_api_pasteboard_change_count", pasteboard_count):
                self._api_pasteboard_change_count = pasteboard_count
                self.refreshModels_(None)


# ================================================================== #
# Windows 앱
# ================================================================== #
elif PLATFORM == "win32":
    class VoiceSTTApp(VoiceSTTCore):
        _ICON_COLORS = {
            "🎙": (80, 80, 80),    # 대기중 — 회색
            "🔴": (220, 50, 50),   # 녹음중 — 빨강
            "⏳": (200, 150, 50),  # 변환중 — 주황
        }

        def __init__(self):
            log.info("VoiceSTTApp (Windows) __init__ 시작")
            sync_ui_language()
            self._root = tk.Tk()
            self._root.withdraw()

            self._status_text = tr("status_idle")
            self._last_text = tr("last", text="-")

            user_cfg = load_user_config()
            api_key_source = (
                "user_config" if user_cfg.get("api_key")
                else "환경변수" if os.environ.get("ELEVENLABS_API_KEY")
                else "없음"
            )
            self._api_key: str = (
                user_cfg.get("api_key")
                or os.environ.get("ELEVENLABS_API_KEY")
                or ""
            )
            log.info("API 키: %s (소스: %s)", "설정됨" if self._api_key else "미설정", api_key_source)

            self._tray = pystray.Icon(
                APP_SLUG,
                self._make_icon_image((80, 80, 80)),
                APP_NAME,
                menu=pystray.Menu(
                    pystray.MenuItem(lambda item: self._status_text, None, enabled=False),
                    pystray.MenuItem(lambda item: self._last_text, None, enabled=False),
                    pystray.Menu.SEPARATOR,
                    pystray.MenuItem(tr("settings"), self._schedule_config_dialog),
                    pystray.MenuItem(tr("restart"), lambda icon, item: self._restart()),
                    pystray.Menu.SEPARATOR,
                    pystray.MenuItem(tr("quit"), lambda icon, item: self._quit()),
                ),
            )
            log.debug("pystray 트레이 아이콘 생성 완료")

            self._core_init()
            self.overlay = RecordingOverlay(self._root)
            log.info("VoiceSTTApp (Windows) __init__ 완료")

        # ── 아이콘 생성 ──

        def _make_icon_image(self, color: tuple) -> PILImage.Image:
            img = PILImage.new("RGBA", (64, 64), (0, 0, 0, 0))
            draw = PILDraw.Draw(img)
            draw.ellipse([4, 4, 60, 60], fill=color + (255,))
            return img

        # ── 추상 메서드 구현 ──

        def _set_tray_title(self, title: str):
            color = self._ICON_COLORS.get(title, (80, 80, 80))
            self._tray.icon = self._make_icon_image(color)
            self._tray.title = f"{APP_NAME} {title}"

        def _set_status(self, status: str):
            self._status_text = status
            self._tray.update_menu()

        def _set_last(self, text: str):
            self._last_text = text
            self._tray.update_menu()

        def _show_microphone_help(self):
            if tkmsgbox.askyesno(APP_NAME, tr("windows_mic_help"), parent=self._root):
                try:
                    os.startfile("ms-settings:privacy-microphone")
                except OSError:
                    log.exception("Windows 마이크 설정 열기 실패")

        # ── UI 큐 (tkinter after 루프) ──

        def _flush_ui_queue_tk(self):
            self._flush_ui_queue_impl()
            self._root.after(50, self._flush_ui_queue_tk)

        def _schedule_config_dialog(self, icon=None, item=None):
            self._root.after(0, self._show_config_dialog)

        def _show_config_dialog(self):
            win = tk.Toplevel(self._root)
            win.title(tr("settings_title", app=APP_NAME))
            win.geometry("520x580")
            win.resizable(False, False)
            values = settings_values(self._api_key)
            displayed_api_key = abbreviate_api_key(self._api_key)
            form = tk.Frame(win, padx=16, pady=14)
            form.pack(fill="both", expand=True)
            entries = {}
            fields = [("api_key", tr("api_key"), 0)]
            for key, label, row in fields:
                tk.Label(form, text=label, anchor="e").grid(
                    row=row, column=0, sticky="e", padx=(0, 12), pady=10)
                entry = tk.Entry(form, width=48)
                entry.insert(0, displayed_api_key if key == "api_key" else values[key])
                entry.grid(row=row, column=1, sticky="ew", pady=10)
                entries[key] = entry
            api_hint = tk.StringVar(value=tr("api_hint"))
            api_hint_label = tk.Label(form, textvariable=api_hint, anchor="w", fg="#777777",
                                      font=("Segoe UI", 8))
            api_hint_label.grid(row=1, column=1, sticky="w", pady=(0, 3))
            tk.Label(form, text=tr("model"), anchor="e").grid(
                row=2, column=0, sticky="e", padx=(0, 12), pady=10)
            model_var = tk.StringVar(value=values["model"])
            model_box = ttk.Combobox(form, textvariable=model_var, width=38, state="readonly")
            model_box["values"] = (values["model"],)
            model_box.grid(row=2, column=1, sticky="w", pady=10)

            def _refresh_models():
                raw_key = entries["api_key"].get().strip()
                api_key = self._api_key if raw_key == displayed_api_key else raw_key
                model_box.set(tr("loading"))
                def _load():
                    try:
                        models = fetch_transcription_models(api_key)
                        if not models:
                            raise RuntimeError(tr("no_models"))
                    except Exception as exc:
                        def _show_error():
                            entries["api_key"].configure(bg="#ffe4e6")
                            api_hint.set(tr("api_key_error"))
                            api_hint_label.configure(fg="#c62828")
                            log.warning("API Key 모델 조회 실패: %s", exc)
                        win.after(0, _show_error)
                        return
                    def _apply():
                        model_box["values"] = models
                        model_box.set(models[0] if model_var.get() not in models else model_var.get())
                        _update_no_verbatim_state()
                        entries["api_key"].configure(bg="white")
                        api_hint.set(tr("api_hint"))
                        api_hint_label.configure(fg="#777777")
                    win.after(0, _apply)
                threading.Thread(target=_load, daemon=True).start()

            tk.Button(form, text=tr("refresh"), command=_refresh_models).grid(
                row=2, column=2, sticky="w", padx=(8, 0))
            # 포커스 이동, Enter, 붙여넣기 어느 경우에도 API Key 확정 후 목록을 갱신한다.
            entries["api_key"].bind("<FocusOut>", lambda _event: _refresh_models())
            entries["api_key"].bind("<Return>", lambda _event: _refresh_models())
            entries["api_key"].bind("<<Paste>>", lambda _event: win.after_idle(_refresh_models), add="+")
            tk.Label(form, text=tr("shortcut"), anchor="e").grid(
                row=3, column=0, sticky="e", padx=(0, 12), pady=10)
            shortcut_items = shortcut_options()
            shortcut_values = [value for value, _ in shortcut_items]
            shortcut_box = ttk.Combobox(form, values=[label for _, label in shortcut_items],
                                       width=38, state="readonly")
            shortcut_box.current(shortcut_values.index(values["shortcut"])
                                 if values["shortcut"] in shortcut_values else 0)
            shortcut_box.grid(row=3, column=1, sticky="w", pady=10)
            tk.Label(form, text=tr("recording_control"), anchor="e").grid(
                row=4, column=0, sticky="e", padx=(0, 12), pady=10)
            control_items = recording_control_options()
            control_values = [value for value, _ in control_items]
            control_box = ttk.Combobox(form, values=[label for _, label in control_items],
                                       width=38, state="readonly")
            control_box.current(control_values.index(values["recording_control"])
                                if values["recording_control"] in control_values else 0)
            control_box.grid(row=4, column=1, sticky="w", pady=10)
            tk.Label(form, text=tr("speech_language"), anchor="e").grid(
                row=5, column=0, sticky="e", padx=(0, 12), pady=10)
            speech_items = speech_language_options()
            if values["language"] not in [code for code, _ in speech_items]:
                speech_items.insert(0, (values["language"], values["language"]))
            speech_codes = [code for code, _ in speech_items]
            speech_box = ttk.Combobox(form, values=[name for _, name in speech_items], width=38, state="readonly")
            speech_box.current(speech_codes.index(values["language"]))
            speech_box.grid(row=5, column=1, sticky="w", pady=10)
            tk.Label(form, text=tr("keywords"), anchor="nw").grid(
                row=7, column=0, sticky="ne", padx=(0, 12), pady=10)
            keyterms = tk.Text(form, width=48, height=8)
            keyterms.insert("1.0", values["keyterms"])
            keyterms.grid(row=7, column=1, sticky="ew", pady=10)
            no_verbatim = tk.BooleanVar(value=values["no_verbatim"])
            mute = tk.BooleanVar(value=values["mute_during_recording"])
            no_verbatim_check = tk.Checkbutton(form, text=tr("remove_fillers"), variable=no_verbatim)
            no_verbatim_check.grid(
                row=8, column=1, sticky="w", pady=(8, 2))
            def _update_no_verbatim_state(_event=None):
                is_supported = (provider_for_api_key(entries["api_key"].get().strip()) == "elevenlabs"
                                and supports_no_verbatim(model_var.get()))
                no_verbatim_check.configure(state="normal" if is_supported else "disabled")
                filler_hint.configure(fg="#777777" if is_supported else "#aaaaaa")
            model_box.bind("<<ComboboxSelected>>", _update_no_verbatim_state)
            filler_hint = tk.Label(form, text=tr("filler_hint"), anchor="w", fg="#777777",
                                   font=("Segoe UI", 8))
            filler_hint.grid(row=9, column=1, sticky="w", pady=(0, 5))
            _update_no_verbatim_state()
            tk.Checkbutton(form, text=tr("mute_audio"), variable=mute).grid(
                row=10, column=1, sticky="w", pady=5)
            auto_send = tk.BooleanVar(value=values["auto_send"])
            tk.Checkbutton(form, text=tr("auto_send"), variable=auto_send).grid(
                row=11, column=1, sticky="w", pady=5)
            form.columnconfigure(1, weight=1)

            def _save():
                values.update({key: entry.get() for key, entry in entries.items()})
                if values["api_key"].strip() == displayed_api_key:
                    values["api_key"] = self._api_key
                values["keyterms"] = keyterms.get("1.0", "end-1c")
                values["shortcut"] = shortcut_values[shortcut_box.current()]
                values["recording_control"] = control_values[control_box.current()]
                values["language"] = speech_codes[speech_box.current()]
                provider = provider_for_api_key(values["api_key"])
                values[f"{provider}_model"] = model_var.get() if provider else values["model"]
                values["auto_send"] = auto_send.get()
                values["no_verbatim"] = no_verbatim.get()
                values["mute_during_recording"] = mute.get()
                save_settings(values)
                sync_ui_language(values["language"])
                self._api_key = values["api_key"].strip()
                self._reload_shortcut_cache()
                tkmsgbox.showinfo(APP_NAME, tr("saved"), parent=win)
                win.destroy()

            buttons = tk.Frame(form)
            buttons.grid(row=12, column=1, sticky="e", pady=(14, 0))
            tk.Button(buttons, text=tr("cancel"), command=win.destroy, width=10).pack(side="right", padx=4)
            tk.Button(buttons, text=tr("save"), command=_save, width=10).pack(side="right")
            entries["api_key"].focus_set()

        # ── 재실행 / 종료 ──

        def _restart(self):
            log.info("재실행 요청")
            subprocess.Popen([sys.executable] + sys.argv)
            self._quit()

        def _quit(self):
            log.info("앱 종료")
            self._tray.stop()
            self._root.quit()

        # ── 메인 루프 ──

        def run(self):
            log.info("Windows 앱 실행 — pystray + tkinter")
            self._tray.run_detached()
            self._root.after(50, self._flush_ui_queue_tk)
            self._root.mainloop()
            log.info("tkinter mainloop 종료")


# ================================================================== #
# 진입점
# ================================================================== #
if __name__ == "__main__":
    _ensure_config_exists()
    _sync_missing_config_keys()
    log.info("run() 호출")
    app = VoiceSTTApp()
    if PLATFORM == "darwin":
        app.run()
    elif PLATFORM == "win32":
        app.run()
    else:
        log.error("지원하지 않는 플랫폼: %s", PLATFORM)
    log.info("run() 종료")
