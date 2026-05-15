import os
import re
import sys
import json
import math
import wave
import time
import queue
import shutil
import logging
import threading
import tempfile
import ctypes
import ctypes.util
import subprocess
from logging.handlers import TimedRotatingFileHandler
from pathlib import Path

try:
    import tomllib
except ImportError:
    import tomli as tomllib  # Python 3.10 호환

import soundfile as sf

PLATFORM = sys.platform  # 'darwin' | 'win32'

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
        from tkinter import scrolledtext
        import tkinter.simpledialog as tkdialog
        import tkinter.messagebox as tkmsgbox
    except ImportError:
        tk = None

# ------------------------------------------------------------------ #
# 로거 설정
# ------------------------------------------------------------------ #
def _get_log_dir() -> Path:
    if PLATFORM == "darwin":
        return Path.home() / "Library" / "Logs" / "voice-stt"
    elif PLATFORM == "win32":
        appdata = os.environ.get("APPDATA", str(Path.home()))
        return Path(appdata) / "voice-stt" / "Logs"
    else:
        return Path.home() / ".local" / "share" / "voice-stt" / "logs"

_LOG_DIR = _get_log_dir()
_LOG_DIR.mkdir(parents=True, exist_ok=True)

_log_handler = TimedRotatingFileHandler(
    _LOG_DIR / "voice-stt.log",
    when="midnight",
    backupCount=1,
    encoding="utf-8",
)
_log_handler.setFormatter(logging.Formatter(
    "%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
))
log = logging.getLogger("voice-stt")
log.setLevel(logging.DEBUG)
log.addHandler(_log_handler)

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
    from pynput import keyboard
    log.info("pynput 로드 OK")
except Exception:
    log.exception("pynput 로드 실패")
    keyboard = None

# ------------------------------------------------------------------ #
# 경로
# ------------------------------------------------------------------ #
CONFIG_PATH = Path(os.path.dirname(os.path.abspath(__file__))) / "config.toml"
CONFIG_EXAMPLE_PATH = Path(os.path.dirname(os.path.abspath(__file__))) / "config.toml.example"

def _get_user_config_path() -> Path:
    if PLATFORM == "darwin":
        return Path.home() / "Library" / "Application Support" / "voice-stt" / "user_config.json"
    elif PLATFORM == "win32":
        appdata = os.environ.get("APPDATA", str(Path.home()))
        return Path(appdata) / "voice-stt" / "user_config.json"
    else:
        return Path.home() / ".config" / "voice-stt" / "user_config.json"

USER_CONFIG_PATH = _get_user_config_path()
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
        return f'"{v}"'
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

    lines = [""]
    for k, v in missing_scalars:
        lines.append(f"{k} = {_toml_format_value(v)}")
    for k, v in missing_tables:
        lines.append(f"\n[{k}]")
        for sk, sv in v.items():
            lines.append(f"{sk} = {_toml_format_value(sv)}")

    with open(CONFIG_PATH, "a", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

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

def load_user_config() -> dict:
    if USER_CONFIG_PATH.exists():
        log.debug("user_config.json 로드: %s", USER_CONFIG_PATH)
        with open(USER_CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        log.debug("user_config.json 로드 완료 — 키: %s", list(data.keys()))
        return data
    log.debug("user_config.json 없음 — 빈 dict 반환")
    return {}

def save_user_config(data: dict):
    USER_CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    log.debug("user_config.json 저장 — 키: %s", list(data.keys()))
    with open(USER_CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    log.debug("user_config.json 저장 완료")

# ------------------------------------------------------------------ #
# 접근성 권한 (macOS 전용)
# ------------------------------------------------------------------ #
def is_accessibility_granted() -> bool:
    if PLATFORM != "darwin":
        return True
    log.debug("접근성 권한 확인 중...")
    try:
        lib = ctypes.cdll.LoadLibrary(ctypes.util.find_library("ApplicationServices"))
        lib.AXIsProcessTrusted.restype = ctypes.c_bool
        result = lib.AXIsProcessTrusted()
        log.debug("접근성 권한: %s", result)
        return result
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

        def show(self, text: str = "🔴  녹음중"):
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

        def show(self, text: str = "🔴  녹음중"):
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
    플랫폼 독립적인 녹음·STT·VAD·키보드 로직.

    서브클래스가 구현해야 하는 메서드:
      _set_tray_title(title)  — 트레이 아이콘/타이틀 변경
      _set_status(status)     — 메뉴의 상태 텍스트 변경
      _set_last(text)         — 마지막 변환 결과 텍스트 변경
      _set_toggle_state(on)   — 메뉴의 "토글 모드" 체크 상태 갱신
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
        self._toggle_listening = False
        self._vad_thread: threading.Thread | None = None
        self._vad_worker_thread: threading.Thread | None = None
        self._vad_segment_queue: queue.Queue = queue.Queue()
        self._cfg_trigger_key = keyboard.Key.alt_r
        self._cfg_toggle_combo: frozenset | None = None
        self._pressed_keys: set = set()
        self.overlay = None
        self._ui_queue: queue.Queue = queue.Queue()
        log.debug("코어 상태 변수 초기화 완료")

        self._reload_shortcut_cache()

        log.info("키보드 리스너 시작 중...")
        listener = keyboard.Listener(
            on_press=self._on_press,
            on_release=self._on_release,
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

    @staticmethod
    def _parse_combo(combo_str: str) -> frozenset | None:
        if not combo_str:
            return None
        keys = []
        for part in combo_str.lower().split("+"):
            k = VoiceSTTCore._key_from_str(part.strip())
            if k is None:
                log.warning("combo 파싱 실패 — 알 수 없는 키: '%s'", part.strip())
                return None
            keys.append(k)
        return frozenset(keys) if keys else None

    def _reload_shortcut_cache(self):
        try:
            config = load_config()
            shortcut = config.get("shortcut", "")
            k = self._key_from_str(shortcut) if shortcut else None
            self._cfg_trigger_key = k if k else keyboard.Key.alt_r
            self._cfg_toggle_combo = self._parse_combo(config.get("toggle_shortcut", ""))
            log.debug("단축키 캐시 갱신 — trigger=%r, toggle_combo=%r",
                      self._cfg_trigger_key, self._cfg_toggle_combo)
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
        if self.overlay and (self.recording or self._transcribing or self._toggle_listening):
            self.overlay.tick()

    # ------------------------------------------------------------------
    # 키 이벤트
    # ------------------------------------------------------------------

    def _on_press(self, key):
        log.debug("키 눌림: %r", key)
        self._pressed_keys.add(key)

        if key == keyboard.Key.esc:
            # 토글 모드 중에는 ESC가 아무 일도 안 한다 — 게임/타이핑 중 ESC가
            # 자주 눌려서 의도치 않게 토글이 꺼지는 문제를 막기 위함.
            # push-to-talk 녹음 취소에만 ESC를 사용한다.
            if not self._toggle_listening and (self.recording or self._transcribing):
                log.info("ESC — 녹음 취소")
                self._cancel_recording()
            return

        if self._cfg_toggle_combo and self._cfg_toggle_combo.issubset(self._pressed_keys):
            if self._toggle_listening:
                log.info("Toggle 단축키 — Toggle 리스닝 OFF")
                self._stop_vad_listening()
            elif not self._transcribing:
                log.info("Toggle 단축키 — Toggle 리스닝 ON")
                self._start_vad_listening()
            return

        if key == self._cfg_trigger_key:
            if self.recording:
                log.debug("트리거 키 눌림 — 이미 녹음중, 무시")
            elif self._transcribing:
                log.debug("트리거 키 눌림 — 변환중, 무시")
            elif self._toggle_listening:
                log.debug("트리거 키 눌림 — Toggle 리스닝 중, 무시")
            else:
                log.info("트리거 키 눌림 — 녹음 시작")
                self._start_recording()

    def _on_release(self, key):
        log.debug("키 뗌: %r", key)
        self._pressed_keys.discard(key)
        if key == self._cfg_trigger_key:
            if self.recording:
                log.info("트리거 키 뗌 — STT 변환 시작")
                self._stop_and_transcribe()
            else:
                log.debug("트리거 키 뗌 — 녹음중 아님 (transcribing=%s)", self._transcribing)

    # ------------------------------------------------------------------
    # 마이크 스트림 생성 (PortAudio 캐시 자동 복구)
    # ------------------------------------------------------------------

    def _create_input_stream(self, **kwargs) -> "sd.InputStream":
        """
        sd.InputStream 생성. PortAudioError 발생 시 PortAudio를 재초기화하고
        디바이스 목록을 새로 읽은 뒤 한 번 재시도한다.
        앱 시작 시점에 마이크가 enumerate 되어 있지 않아 디폴트 디바이스 캐시가
        비어있는 상태에서 회복하기 위함.
        """
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
        # Windows는 항상 type 모드 — 레거시 게임(스타크래프트 등) 채팅이 Ctrl+V를 무시함
        text, triggered_keys = self._process_key_triggers(text)
        log.debug("_paste_text — %d자", len(text))
        kb = keyboard.Controller()
        if PLATFORM == "win32":
            kb.type(text)
            log.info("타이핑 입력 완료 (%d자)", len(text))
        else:
            pyperclip.copy(text)
            log.info("클립보드 복사 완료")
            with kb.pressed(keyboard.Key.cmd):
                kb.press("v")
                kb.release("v")
        time.sleep(0.1)
        kb.press(keyboard.Key.enter)
        kb.release(keyboard.Key.enter)
        for kw, key in triggered_keys:
            time.sleep(0.05)
            kb.press(key)
            kb.release(key)
            log.info("custom_key_trigger 실행 — %r", kw)
        log.info("붙여넣기 + Enter 완료")

    def _process_key_triggers(self, text: str) -> tuple[str, list]:
        """텍스트에서 custom_key_trigger 키워드를 찾아 제거하고 트리거할 키 목록을 반환"""
        config = load_config()
        triggers = config.get("custom_key_trigger", {})
        triggered_keys = []
        for keyword, key_str in triggers.items():
            if keyword in text:
                text = text.replace(keyword, "")
                text = " ".join(text.split())
                key = self._key_from_str(key_str)
                if key:
                    triggered_keys.append((keyword, key))
                    log.debug("custom_key_trigger 감지 — keyword=%r key=%r", keyword, key_str)
        return text, triggered_keys

    def _send_before_and_paste(self, text: str):
        config = load_config()
        before_key_str = config.get("toggle_before_key", "enter")
        after_key_str = config.get("toggle_after_key", "enter")

        text, triggered_keys = self._process_key_triggers(text)

        before_key = self._key_from_str(before_key_str) if before_key_str else None
        after_key = self._key_from_str(after_key_str) if after_key_str else None
        log.debug("_send_before_and_paste — before=%r, after=%r, triggers=%d, %d자",
                  before_key, after_key, len(triggered_keys), len(text))
        kb = keyboard.Controller()

        if before_key:
            kb.press(before_key)
            kb.release(before_key)
            time.sleep(0.15)

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

        for kw, key in triggered_keys:
            kb.press(key)
            kb.release(key)
            time.sleep(0.05)
            log.info("custom_key_trigger 실행 — %r", kw)

        log.info("toggle_before_and_paste 완료")

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
            self._set_status("상태: 취소됨"),
            self.overlay.hide() if self.overlay else None,
        ))
        if self._reset_timer:
            self._reset_timer.cancel()

        def _on_cancel():
            self._ui(lambda: self._set_status("상태: 대기중"))

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
            self._set_status("상태: 녹음중..."),
            self.overlay.show("🔴  녹음중") if self.overlay else None,
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
                self._set_status("상태: 마이크 오류"),
                self.overlay.show("❌  마이크 없음") if self.overlay else None,
            ))
            if self._reset_timer:
                self._reset_timer.cancel()

            def _on_mic_error_reset():
                self._ui(lambda: (
                    self._set_status("상태: 대기중"),
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
            self._set_status("상태: 변환중..."),
            self.overlay.show("⏳  변환중...") if self.overlay else None,
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
                            _holder["result"] = client.speech_to_text.convert(
                                file=f,
                                model_id="scribe_v2",
                                language_code=config.get("language", "ko"),
                                keyterms=config.get("keyterms", []) or None,
                                no_verbatim=config.get("no_verbatim", True),
                            )
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
                        self._set_status("상태: 재시도중..."),
                        self.overlay.show("🔄  재시도중...") if self.overlay else None,
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

            result = self._call_elevenlabs(config, api_key, tmp_path, hard_timeout, sdk_timeout)

            if self._cancelled:
                return

            text = self._clean_text(result.text or "")
            log.info("변환 결과: %d자 — %r", len(text), text[:80])

            if text:
                preview = text[:40] + ("..." if len(text) > 40 else "")
                self._ui(lambda t=text, p=preview: (
                    self._paste_text(t),
                    self._set_last(f"마지막 변환: {p}"),
                    self._set_status("상태: 대기중"),
                ))
            else:
                log.warning("변환 결과 텍스트 없음")
                self._ui(lambda: self._set_status("상태: 텍스트 없음"))

        except Exception as e:
            error_occurred = True
            log.exception("STT 오류: [%s] %s", type(e).__name__, e)
            if not self._cancelled:
                self._ui(lambda: (
                    self._set_status("상태: 실패"),
                    self.overlay.show("❌  실패했습니다") if self.overlay else None,
                ))
                if self._reset_timer:
                    self._reset_timer.cancel()

                def _on_error():
                    self._ui(lambda: (
                        self._set_status("상태: 대기중"),
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
    # Toggle 모드 (VAD 연속 리스닝)
    # ------------------------------------------------------------------

    def _start_vad_listening(self):
        self._toggle_listening = True
        # 이전 세션에 남은 구간 비우기
        while not self._vad_segment_queue.empty():
            try:
                self._vad_segment_queue.get_nowait()
            except queue.Empty:
                break
        self._vad_thread = threading.Thread(target=self._vad_loop, daemon=True)
        self._vad_thread.start()
        self._vad_worker_thread = threading.Thread(target=self._vad_worker, daemon=True)
        self._vad_worker_thread.start()
        log.info("VAD 리스닝 + 워커 시작")
        self._ui(lambda: (
            self._set_tray_title("👂"),
            self._set_status("상태: 듣는중 (Toggle ON)"),
            self._set_toggle_state(True),
            self.overlay.show("👂  듣는중...") if self.overlay else None,
        ))

    def _stop_vad_listening(self):
        self._toggle_listening = False
        log.info("VAD 리스닝 중지 요청")
        self._ui(lambda: (
            self._set_tray_title("🎙"),
            self._set_status("상태: 대기중"),
            self._set_toggle_state(False),
            self.overlay.hide() if self.overlay else None,
        ))

    def _request_toggle_mode(self):
        """메뉴 클릭 등 외부에서 토글 모드 전환을 요청한다."""
        if self._toggle_listening:
            log.info("메뉴 — Toggle 리스닝 OFF")
            self._stop_vad_listening()
        elif self._transcribing:
            log.debug("메뉴 — 변환중이라 토글 요청 무시")
        else:
            log.info("메뉴 — Toggle 리스닝 ON")
            self._start_vad_listening()

    def _vad_loop(self):
        config = load_config()
        threshold = config.get("vad_threshold", 500)
        silence_sec = config.get("vad_silence_sec", 1.0)
        chunk_sec = 0.05
        chunk_samples = int(self.SAMPLE_RATE * chunk_sec)
        silence_chunks_needed = int(silence_sec / chunk_sec)
        chunk_q: queue.Queue = queue.Queue()

        def _vad_callback(indata, frames, time_info, status):
            chunk_q.put(indata.copy())
            rms = float(np.sqrt(np.mean(indata.astype(np.float32) ** 2)))
            if self.overlay:
                # threshold를 0점으로 재정규화 — 실제로 STT에 들어가는 소리만 시각화
                self.overlay.set_volume(min(1.0, max(0.0, rms - threshold) / 4000.0))

        speech_frames: list[np.ndarray] = []
        silence_count = 0
        speaking = False

        log.info("VAD 루프 진입 — threshold=%d, silence_sec=%.1f", threshold, silence_sec)
        diag_rms: list[float] = []
        diag_chunks_target = int(1.0 / chunk_sec)  # 처음 1초만 수집
        diag_logged = False
        try:
            with self._create_input_stream(
                samplerate=self.SAMPLE_RATE,
                channels=1,
                dtype="int16",
                blocksize=chunk_samples,
                callback=_vad_callback,
            ):
                while self._toggle_listening:
                    try:
                        chunk = chunk_q.get(timeout=0.1)
                    except queue.Empty:
                        continue

                    rms = float(np.sqrt(np.mean(chunk.astype(np.float32) ** 2)))

                    if not diag_logged:
                        diag_rms.append(rms)
                        if len(diag_rms) >= diag_chunks_target:
                            avg = sum(diag_rms) / len(diag_rms)
                            mx = max(diag_rms)
                            mn = min(diag_rms)
                            log.info(
                                "VAD 진단 — 첫 1초 RMS: avg=%.0f, min=%.0f, max=%.0f, threshold=%d",
                                avg, mn, mx, threshold,
                            )
                            if mn >= threshold:
                                log.warning(
                                    "VAD 진단: 최저 RMS(%.0f)도 임계값(%d) 이상 — "
                                    "침묵 감지 불가. config.toml의 vad_threshold를 %d 이상으로 올리세요.",
                                    mn, threshold, int(mn * 1.5),
                                )
                            diag_logged = True

                    if rms >= threshold:
                        if not speaking:
                            speaking = True
                            silence_count = 0
                            log.info("VAD: 음성 감지 시작")
                            self._ui(lambda: (
                                self._set_tray_title("🔴"),
                                self._set_status("상태: 녹음중 (VAD)"),
                                self.overlay.show("🔴  녹음중") if self.overlay else None,
                            ))
                        else:
                            silence_count = 0
                        speech_frames.append(chunk)
                    elif speaking:
                        silence_count += 1
                        speech_frames.append(chunk)

                        if silence_count >= silence_chunks_needed:
                            frames_to_send = (speech_frames[:-silence_count]
                                              if silence_count < len(speech_frames) else speech_frames)
                            speech_frames = []
                            silence_count = 0
                            speaking = False

                            if frames_to_send:
                                self._vad_segment_queue.put(frames_to_send)
                                log.info("VAD: 음성 구간 → 대기열 (%d 청크, 대기 %d개)",
                                         len(frames_to_send), self._vad_segment_queue.qsize())
        except Exception:
            log.exception("VAD 루프 오류 — 마이크 사용 불가")
            # 토글 모드를 자동으로 OFF 시키고 사용자에게 알린다
            self._toggle_listening = False
            self._ui(lambda: (
                self._set_tray_title("🎙"),
                self._set_status("상태: 마이크 오류"),
                self._set_toggle_state(False),
                self.overlay.show("❌  마이크 없음") if self.overlay else None,
            ))
            if self._reset_timer:
                self._reset_timer.cancel()

            def _on_vad_mic_error_reset():
                self._ui(lambda: (
                    self._set_status("상태: 대기중"),
                    self.overlay.hide() if self.overlay else None,
                ))

            self._reset_timer = threading.Timer(3.0, _on_vad_mic_error_reset)
            self._reset_timer.start()
        finally:
            log.info("VAD 루프 종료")

    def _vad_worker(self):
        """VAD 세그먼트 큐를 순서대로 처리하는 워커. VAD 루프와 독립적으로 동작."""
        log.info("VAD 워커 진입")
        while self._toggle_listening:
            try:
                frames = self._vad_segment_queue.get(timeout=0.2)
            except queue.Empty:
                continue
            log.info("VAD 워커: 세그먼트 처리 시작 (%d 청크, 남은 대기 %d개)",
                     len(frames), self._vad_segment_queue.qsize())
            self._transcribing = True
            self._ui(lambda: (
                self._set_tray_title("⏳"),
                self._set_status("상태: 변환중..."),
                self.overlay.show("⏳  변환중...") if self.overlay else None,
            ))
            self._transcribe_vad(frames)  # 동기 호출 — 완료될 때까지 대기
        log.info("VAD 워커 종료")

    def _transcribe_vad(self, frames: list[np.ndarray]):
        t_start = time.time()
        tmp_path = None
        try:
            config = load_config()
            api_key = self._api_key
            if not api_key:
                raise ValueError("API Key가 설정되지 않았습니다.")

            audio_data = np.concatenate(frames, axis=0)
            duration_sec = len(audio_data) / self.SAMPLE_RATE
            log.info("VAD STT 시작 — %.1f초", duration_sec)

            with tempfile.NamedTemporaryFile(suffix=".flac", delete=False) as f:
                tmp_path = f.name
            sf.write(tmp_path, audio_data, self.SAMPLE_RATE, format="FLAC", subtype="PCM_16")

            hard_timeout = max(5.0, duration_sec + 10.0)
            sdk_timeout = hard_timeout - 2.0

            result = self._call_elevenlabs(config, api_key, tmp_path, hard_timeout, sdk_timeout)

            text = self._clean_text(result.text or "")
            if text:
                preview = text[:40] + ("..." if len(text) > 40 else "")
                log.info("VAD 변환 결과: %d자 — %r", len(text), text[:40])
                self._ui(lambda t=text, p=preview: (
                    self._send_before_and_paste(t),
                    self._set_last(f"마지막 변환: {p}"),
                ))
            else:
                log.warning("VAD STT: 텍스트 없음")

        except Exception:
            log.exception("VAD STT 오류 — 소요=%.2f초", time.time() - t_start)
        finally:
            if tmp_path and os.path.exists(tmp_path):
                try:
                    os.unlink(tmp_path)
                except Exception:
                    pass
            self._transcribing = False
            # 큐에 다음 구간이 있으면 워커가 곧 다시 ⏳로 바꾸므로 잠깐 듣는중 표시
            if self._toggle_listening:
                self._ui(lambda: (
                    self._set_tray_title("👂"),
                    self._set_status("상태: 듣는중 (Toggle ON)"),
                    self.overlay.show("👂  듣는중...") if self.overlay else None,
                ))
            else:
                self._ui(lambda: (
                    self._set_tray_title("🎙"),
                    self._set_status("상태: 대기중"),
                    self.overlay.hide() if self.overlay else None,
                ))


# ================================================================== #
# macOS 앱
# ================================================================== #
if PLATFORM == "darwin":
    class VoiceSTTApp(rumps.App, VoiceSTTCore):
        def __init__(self):
            log.info("VoiceSTTApp (macOS) __init__ 시작")
            rumps.App.__init__(self, "🎙", quit_button="종료")

            self.status_item = rumps.MenuItem("상태: 대기중")
            self.last_item = rumps.MenuItem("마지막 변환: -")
            self.toggle_mode_item = rumps.MenuItem("토글 모드", callback=self._on_toggle_mode)
            self.apikey_item = rumps.MenuItem("API Key 설정...", callback=self._on_set_api_key)
            self.config_item = rumps.MenuItem("설정...", callback=self._on_edit_config)
            self.restart_item = rumps.MenuItem("재실행", callback=self._restart)
            log.debug("메뉴 아이템 생성 완료")

            user_cfg = load_user_config()
            api_key_source = (
                "user_config" if user_cfg.get("api_key")
                else "환경변수" if os.environ.get("ELEVENLABS_API_KEY")
                else "config.toml" if load_config().get("api_key")
                else "없음"
            )
            self._api_key: str = (
                user_cfg.get("api_key")
                or os.environ.get("ELEVENLABS_API_KEY")
                or load_config().get("api_key")
                or ""
            )
            log.info("API 키: %s (소스: %s)", "설정됨" if self._api_key else "미설정", api_key_source)

            accessibility = is_accessibility_granted()
            log.info("접근성 권한: %s", accessibility)
            if not accessibility:
                self._accessibility_item = rumps.MenuItem(
                    "⚠️ 접근성 권한 필요 — 클릭하여 설정 열기",
                    callback=self._open_accessibility_prefs,
                )
                self.menu = [self.status_item, self.last_item, None, self._accessibility_item,
                             None, self.toggle_mode_item, None,
                             self.apikey_item, self.config_item, None, self.restart_item, None]
            else:
                self.menu = [self.status_item, self.last_item, None, self.toggle_mode_item, None,
                             self.apikey_item, self.config_item, None, self.restart_item, None]

            self._core_init()
            log.info("VoiceSTTApp (macOS) __init__ 완료")

        # ── 추상 메서드 구현 ──

        def _set_tray_title(self, title: str):
            self.title = title

        def _set_status(self, status: str):
            self.status_item.title = status

        def _set_last(self, text: str):
            self.last_item.title = text

        def _set_toggle_state(self, on: bool):
            self.toggle_mode_item.state = 1 if on else 0

        def _on_toggle_mode(self, _):
            self._request_toggle_mode()

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

        # ── 접근성 권한 ──

        def _open_accessibility_prefs(self, _):
            log.info("접근성 권한 설정 열기")
            subprocess.Popen([
                "open",
                "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility",
            ])
            rumps.notification("voice-stt", "", "권한 허용 후 앱을 재시작해 주세요.")

        # ── 재실행 ──

        def _restart(self, _):
            exe = sys.executable
            log.info("재실행 요청 — exe: %s", exe)
            if ".app/Contents" in exe:
                bundle = exe[:exe.index(".app/Contents") + 4]
                subprocess.Popen(["open", bundle])
            else:
                subprocess.Popen([sys.executable] + sys.argv)
            log.info("재실행 프로세스 시작 — 현재 앱 종료")
            rumps.quit_application()

        # ── API Key 설정 ──

        def _on_set_api_key(self, _):
            log.info("API Key 설정 창 열기")
            window = rumps.Window(
                title="ElevenLabs API Key 설정",
                message="ElevenLabs API Key를 입력하세요.\n(elevenlabs.io → Profile → API Keys)",
                default_text=self._api_key,
                ok="저장",
                cancel="취소",
                dimensions=(420, 24),
            )
            response = window.run()
            if response.clicked:
                new_key = response.text.strip()
                if new_key:
                    self._api_key = new_key
                    save_user_config({"api_key": new_key})
                    log.info("API 키 저장됨")
                    rumps.notification("voice-stt", "", "API Key가 저장되었습니다.")
                else:
                    log.warning("API Key 입력 없음")
                    rumps.alert(title="voice-stt", message="API Key를 입력해 주세요.")

        # ── 설정 편집 ──

        def _on_edit_config(self, _):
            log.info("설정 편집 창 열기")
            try:
                with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                    current = f.read()
            except Exception:
                log.exception("config.toml 로드 실패")
                current = ""

            from AppKit import (
                NSAlert, NSScrollView, NSTextView, NSMakeRect, NSFont, NSBezelBorder,
            )
            W, H = 600, 440
            scroll = NSScrollView.alloc().initWithFrame_(NSMakeRect(0, 0, W, H))
            scroll.setHasVerticalScroller_(True)
            scroll.setHasHorizontalScroller_(False)
            scroll.setAutohidesScrollers_(False)
            scroll.setBorderType_(NSBezelBorder)
            tv = NSTextView.alloc().initWithFrame_(NSMakeRect(0, 0, W, H))
            tv.setString_(current)
            tv.setFont_(NSFont.fontWithName_size_("Menlo", 12))
            tv.setAutomaticQuoteSubstitutionEnabled_(False)
            tv.setAutomaticDashSubstitutionEnabled_(False)
            tv.setRichText_(False)
            scroll.setDocumentView_(tv)
            alert = NSAlert.alloc().init()
            alert.setMessageText_("설정 (config.toml)")
            alert.setInformativeText_("TOML을 수정한 후 저장하세요.")
            alert.addButtonWithTitle_("저장")
            alert.addButtonWithTitle_("취소")
            alert.setAccessoryView_(scroll)
            alert.window().setInitialFirstResponder_(tv)
            response = alert.runModal()
            if response == 1000:
                text = tv.string().strip()
                try:
                    tomllib.loads(text)
                except tomllib.TOMLDecodeError as e:
                    log.warning("TOML 파싱 오류: %s", e)
                    rumps.alert(title="voice-stt", message=f"TOML 오류:\n{e}")
                    return
                with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                    f.write(text)
                log.info("config.toml 저장됨")
                self._reload_shortcut_cache()
                rumps.notification("voice-stt", "", "설정이 저장되었습니다.")
            else:
                log.debug("설정 편집 취소됨")


# ================================================================== #
# Windows 앱
# ================================================================== #
elif PLATFORM == "win32":
    class VoiceSTTApp(VoiceSTTCore):
        _ICON_COLORS = {
            "🎙": (80, 80, 80),    # 대기중 — 회색
            "🔴": (220, 50, 50),   # 녹음중 — 빨강
            "⏳": (200, 150, 50),  # 변환중 — 주황
            "👂": (50, 120, 220),  # 듣는중 — 파랑
        }

        def __init__(self):
            log.info("VoiceSTTApp (Windows) __init__ 시작")
            self._root = tk.Tk()
            self._root.withdraw()

            self._status_text = "상태: 대기중"
            self._last_text = "마지막 변환: -"

            user_cfg = load_user_config()
            api_key_source = (
                "user_config" if user_cfg.get("api_key")
                else "환경변수" if os.environ.get("ELEVENLABS_API_KEY")
                else "config.toml" if load_config().get("api_key")
                else "없음"
            )
            self._api_key: str = (
                user_cfg.get("api_key")
                or os.environ.get("ELEVENLABS_API_KEY")
                or load_config().get("api_key")
                or ""
            )
            log.info("API 키: %s (소스: %s)", "설정됨" if self._api_key else "미설정", api_key_source)

            self._tray = pystray.Icon(
                "voice-stt",
                self._make_icon_image((80, 80, 80)),
                "voice-stt",
                menu=pystray.Menu(
                    pystray.MenuItem(lambda item: self._status_text, None, enabled=False),
                    pystray.MenuItem(lambda item: self._last_text, None, enabled=False),
                    pystray.Menu.SEPARATOR,
                    pystray.MenuItem(
                        "토글 모드",
                        lambda icon, item: self._on_toggle_mode(),
                        checked=lambda item: self._toggle_listening,
                    ),
                    pystray.Menu.SEPARATOR,
                    pystray.MenuItem("API Key 설정...", self._schedule_api_key_dialog),
                    pystray.MenuItem("설정...", self._schedule_config_dialog),
                    pystray.MenuItem("재실행", lambda icon, item: self._restart()),
                    pystray.Menu.SEPARATOR,
                    pystray.MenuItem("종료", lambda icon, item: self._quit()),
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
            self._tray.title = f"voice-stt {title}"

        def _set_status(self, status: str):
            self._status_text = status
            self._tray.update_menu()

        def _set_last(self, text: str):
            self._last_text = text
            self._tray.update_menu()

        def _set_toggle_state(self, on: bool):
            # pystray의 checked 람다가 _toggle_listening을 참조하므로 메뉴만 갱신
            self._tray.update_menu()

        def _on_toggle_mode(self):
            self._request_toggle_mode()

        # ── UI 큐 (tkinter after 루프) ──

        def _flush_ui_queue_tk(self):
            self._flush_ui_queue_impl()
            self._root.after(50, self._flush_ui_queue_tk)

        # ── API Key 설정 ──

        def _schedule_api_key_dialog(self, icon=None, item=None):
            self._root.after(0, self._show_api_key_dialog)

        def _show_api_key_dialog(self):
            log.info("API Key 설정 창 열기")
            new_key = tkdialog.askstring(
                "API Key 설정",
                "ElevenLabs API Key를 입력하세요:\n(elevenlabs.io → Profile → API Keys)",
                initialvalue=self._api_key,
                parent=self._root,
            )
            if new_key is not None:
                new_key = new_key.strip()
                if new_key:
                    self._api_key = new_key
                    save_user_config({"api_key": new_key})
                    log.info("API 키 저장됨")
                    tkmsgbox.showinfo("voice-stt", "API Key가 저장되었습니다.", parent=self._root)
                else:
                    log.warning("API Key 입력 없음")
                    tkmsgbox.showwarning("voice-stt", "API Key를 입력해 주세요.", parent=self._root)

        # ── 설정 편집 ──

        def _schedule_config_dialog(self, icon=None, item=None):
            self._root.after(0, self._show_config_dialog)

        def _show_config_dialog(self):
            log.info("설정 편집 창 열기")
            try:
                with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                    current = f.read()
            except Exception:
                log.exception("config.toml 로드 실패")
                current = ""

            win = tk.Toplevel(self._root)
            win.title("설정 (config.toml) — Ctrl+S 저장")
            win.geometry("760x560")
            win.minsize(600, 400)
            win.resizable(True, True)
            # 부모(_root)가 withdraw 상태이므로 transient는 쓰지 않는다 — 일부 환경에서 자식 창이 같이 숨겨진다
            try:
                win.deiconify()
                win.lift()
                win.attributes('-topmost', True)
                win.after(500, lambda: win.attributes('-topmost', False))
                win.focus_force()
            except Exception:
                log.exception("설정 창 표시 속성 설정 실패")

            # 버튼을 먼저 pack해서 창이 좁아져도 항상 보이게 한다
            btn_frame = tk.Frame(win)
            btn_frame.pack(side='bottom', fill='x', padx=10, pady=10)

            text_widget = scrolledtext.ScrolledText(win, width=90, height=28, font=("Consolas", 11))
            text_widget.pack(side='top', fill='both', expand=True, padx=10, pady=(10, 0))
            text_widget.insert('1.0', current)

            def _save():
                log.info("설정 다이얼로그 — 저장 클릭")
                text = text_widget.get('1.0', 'end').strip()
                try:
                    tomllib.loads(text)
                except tomllib.TOMLDecodeError as e:
                    log.warning("TOML 파싱 오류: %s", e)
                    tkmsgbox.showerror("TOML 오류", str(e), parent=win)
                    return
                with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                    f.write(text)
                log.info("config.toml 저장됨")
                self._reload_shortcut_cache()
                tkmsgbox.showinfo("voice-stt", "설정이 저장되었습니다.", parent=win)
                win.destroy()

            def _close_with_check():
                if text_widget.get('1.0', 'end').strip() != current.strip():
                    if not tkmsgbox.askokcancel(
                        "변경 사항 버리기",
                        "저장하지 않은 변경 사항이 있습니다. 정말 닫을까요?",
                        parent=win,
                    ):
                        return
                win.destroy()

            save_btn = tk.Button(
                btn_frame, text="저장 (Ctrl+S)", command=_save,
                width=14, height=2, bg='#2d7dd2', fg='white',
                activebackground='#225fa3', activeforeground='white',
                font=('Segoe UI', 10, 'bold'),
            )
            save_btn.pack(side='right', padx=4)
            tk.Button(
                btn_frame, text="취소", command=_close_with_check,
                width=10, height=2, font=('Segoe UI', 10),
            ).pack(side='right')

            win.bind('<Control-s>', lambda e: _save())
            win.bind('<Control-S>', lambda e: _save())
            win.protocol("WM_DELETE_WINDOW", _close_with_check)
            text_widget.focus_set()

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
