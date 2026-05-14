import os
import json
import math
import wave
import time
import queue
import logging
import threading
import tempfile
import ctypes
import ctypes.util
from logging.handlers import TimedRotatingFileHandler
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env"))

# ------------------------------------------------------------------
# 로거 설정 — ~/Library/Logs/voice-stt/voice-stt.log
# 자정마다 롤오버, 하루치(backupCount=1)만 보관
# ------------------------------------------------------------------
_LOG_DIR = Path.home() / "Library" / "Logs" / "voice-stt"
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

import sys
log.info("=" * 60)
log.info("앱 시작 — Python %s | frozen=%s | pid=%d", sys.version.split()[0], getattr(sys, "frozen", False), os.getpid())
log.debug("sys.executable: %s", sys.executable)
log.debug("sys.argv: %s", sys.argv)

# ------------------------------------------------------------------
# SSL — 빌드된 앱(.app)에서 CA 파일 경로를 못 찾는 문제 우회
# ------------------------------------------------------------------
import ssl
import certifi as _certifi_mod
try:
    _ca_path = _certifi_mod.where()
    log.debug("certifi CA 파일 경로: %s", _ca_path)
    with open(_ca_path, "r", encoding="ascii") as _f:
        _CA_DATA = _f.read()
    log.debug("certifi CA 데이터 로드 완료 — %d bytes", len(_CA_DATA))
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

try:
    import numpy as np
    log.info("numpy 로드 OK — version=%s", np.__version__)
except Exception:
    log.exception("numpy 로드 실패")

try:
    import pyperclip
    log.info("pyperclip 로드 OK")
except Exception:
    log.exception("pyperclip 로드 실패")

try:
    import rumps
    log.info("rumps 로드 OK")
except Exception:
    log.exception("rumps 로드 실패")

try:
    import sounddevice as sd
    log.info("sounddevice 로드 OK — version=%s", sd.__version__)
    try:
        log.debug("기본 입력 장치: %s", sd.query_devices(kind='input'))
    except Exception:
        log.debug("기본 입력 장치 조회 실패")
except Exception:
    log.exception("sounddevice 로드 실패")

try:
    from elevenlabs.client import ElevenLabs
    log.info("elevenlabs 로드 OK")
except Exception:
    log.exception("elevenlabs 로드 실패")

try:
    from pynput import keyboard
    log.info("pynput 로드 OK")
except Exception:
    log.exception("pynput 로드 실패")

CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
USER_CONFIG_PATH = Path.home() / "Library" / "Application Support" / "voice-stt" / "user_config.json"
log.info("CONFIG_PATH: %s", CONFIG_PATH)
log.debug("USER_CONFIG_PATH: %s", USER_CONFIG_PATH)


def is_accessibility_granted() -> bool:
    log.debug("접근성 권한 확인 중...")
    lib = ctypes.cdll.LoadLibrary(ctypes.util.find_library("ApplicationServices"))
    lib.AXIsProcessTrusted.restype = ctypes.c_bool
    result = lib.AXIsProcessTrusted()
    log.debug("접근성 권한 결과: %s", result)
    return result


def load_config() -> dict:
    log.debug("config.json 로드: %s", CONFIG_PATH)
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)
    log.debug("config.json 로드 완료 — 키: %s", list(data.keys()))
    return data


def load_user_config() -> dict:
    if USER_CONFIG_PATH.exists():
        log.debug("user_config.json 로드: %s", USER_CONFIG_PATH)
        with open(USER_CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        log.debug("user_config.json 로드 완료 — 키: %s", list(data.keys()))
        return data
    log.debug("user_config.json 없음 (%s) — 빈 dict 반환", USER_CONFIG_PATH)
    return {}


def save_user_config(data: dict):
    USER_CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    log.debug("user_config.json 저장 중 — 키: %s", list(data.keys()))
    with open(USER_CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    log.debug("user_config.json 저장 완료")


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
                log.debug("오버레이: NSWindowStyleMaskBorderless 사용")
            except ImportError:
                try:
                    from AppKit import NSBorderlessWindowMask as _borderless
                    log.debug("오버레이: NSBorderlessWindowMask 사용 (구버전 폴백)")
                except ImportError:
                    _borderless = 0
                    log.debug("오버레이: borderless mask = 0 (폴백)")
            try:
                from AppKit import NSWindowLevelFloating as _floating_level
                log.debug("오버레이: NSWindowLevelFloating 사용")
            except ImportError:
                try:
                    from AppKit import NSFloatingWindowLevel as _floating_level
                    log.debug("오버레이: NSFloatingWindowLevel 사용 (구버전 폴백)")
                except ImportError:
                    _floating_level = 3
                    log.debug("오버레이: floating level = 3 (폴백)")
            try:
                from AppKit import NSTextAlignmentLeft as _align_left
            except ImportError:
                _align_left = 0

            screen = NSScreen.mainScreen()
            vis = screen.visibleFrame()
            full = screen.frame()
            x = (full.size.width - self.WIDTH) / 2
            y = vis.origin.y + 40
            log.debug("오버레이 초기 위치: (%.0f, %.0f), 화면 크기: %.0fx%.0f, 가시 영역 y: %.0f",
                      x, y, full.size.width, full.size.height, vis.origin.y)

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
            log.debug("오버레이 NSWindow 생성 완료")

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
            log.debug("오버레이 레이블 생성 완료")

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
                log.info("오버레이 볼륨 바 초기화 OK (%d개)", self.BAR_COUNT)
            except Exception:
                log.exception("오버레이 볼륨 바 초기화 실패 — 바 없이 계속")

            self._available = True
            log.info("오버레이 초기화 완료")
        except Exception:
            log.exception("오버레이 초기화 실패 — 오버레이 없이 계속")
            self._available = False

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
            log.debug("오버레이 표시: text='%s', 위치=(%.0f, %.0f)", text, x, y)
            self._label.setStringValue_(text)
            cur = self._win.frame()
            self._win.setFrame_display_(NSMakeRect(x, y, cur.size.width, cur.size.height), False)
            self._win.orderFrontRegardless()
        else:
            log.debug("오버레이 표시 시도: 오버레이 비활성 상태 — 무시")

    def hide(self):
        if self._available:
            log.debug("오버레이 숨김")
            self._volume = 0.0
            self._win.orderOut_(None)
        else:
            log.debug("오버레이 숨김 시도: 오버레이 비활성 상태 — 무시")


class VoiceSTTApp(rumps.App):
    SAMPLE_RATE = 16000

    def __init__(self):
        log.info("VoiceSTTApp.__init__ 시작")
        super().__init__("🎙", quit_button="종료")
        log.info("rumps.App 초기화 완료")

        self.recording = False
        self._transcribing = False
        self._cancelled = False
        self._reset_timer: threading.Timer | None = None
        self.audio_frames: list[np.ndarray] = []
        self.stream: sd.InputStream | None = None
        log.debug("상태 변수 초기화 완료 — recording=False, _transcribing=False, _cancelled=False")

        user_cfg = load_user_config()
        api_key_source = (
            "user_config" if user_cfg.get("api_key")
            else "환경변수" if os.environ.get("ELEVENLABS_API_KEY")
            else "config.json" if load_config().get("api_key")
            else "없음"
        )
        self._api_key: str = (
            user_cfg.get("api_key")
            or os.environ.get("ELEVENLABS_API_KEY")
            or load_config().get("api_key")
            or ""
        )
        log.info("API 키 로드: %s (소스: %s)", "설정됨" if self._api_key else "미설정", api_key_source)

        self.status_item = rumps.MenuItem("상태: 대기중")
        self.last_item = rumps.MenuItem("마지막 변환: -")
        self.apikey_item = rumps.MenuItem("API Key 설정...", callback=self._on_set_api_key)
        self.config_item = rumps.MenuItem("설정...", callback=self._on_edit_config)
        self.restart_item = rumps.MenuItem("재실행", callback=self._restart)
        log.debug("메뉴 아이템 생성 완료")

        accessibility = is_accessibility_granted()
        log.info("접근성 권한: %s", accessibility)
        if not accessibility:
            self._accessibility_item = rumps.MenuItem(
                "⚠️ 접근성 권한 필요 — 클릭하여 설정 열기",
                callback=self._open_accessibility_prefs,
            )
            self.menu = [self.status_item, self.last_item, None, self._accessibility_item, None, self.apikey_item, self.config_item, None, self.restart_item, None]
            log.warning("접근성 권한 없음 — 키보드 입력 시뮬레이션 불가")
        else:
            self._accessibility_item = None
            self.menu = [self.status_item, self.last_item, None, self.apikey_item, self.config_item, None, self.restart_item, None]

        self.overlay: RecordingOverlay | None = None
        self._ui_queue: queue.Queue = queue.Queue()
        log.debug("UI 큐 초기화 완료")

        log.info("키보드 리스너 시작 중...")
        listener = keyboard.Listener(
            on_press=self._on_press,
            on_release=self._on_release,
        )
        listener.daemon = True
        threading.Thread(target=listener.start, daemon=True).start()
        log.info("키보드 리스너 스레드 시작됨")
        log.info("VoiceSTTApp.__init__ 완료 — run() 호출 대기")

    # ------------------------------------------------------------------
    # 접근성 권한 안내
    # ------------------------------------------------------------------

    def _open_accessibility_prefs(self, _):
        import subprocess
        log.info("접근성 권한 설정 열기 요청")
        subprocess.Popen([
            "open",
            "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility",
        ])
        rumps.notification("voice-stt", "", "권한 허용 후 앱을 재시작해 주세요.")

    def _restart(self, _):
        import subprocess
        import sys
        exe = sys.executable
        log.info("재실행 요청 — exe: %s", exe)
        if ".app/Contents" in exe:
            bundle = exe[:exe.index(".app/Contents") + 4]
            log.debug("앱 번들 재실행: %s", bundle)
            subprocess.Popen(["open", bundle])
        else:
            log.debug("스크립트 재실행: %s %s", sys.executable, sys.argv)
            subprocess.Popen([sys.executable] + sys.argv)
        log.info("재실행 프로세스 시작 — 현재 앱 종료")
        rumps.quit_application()

    # ------------------------------------------------------------------
    # API Key 설정
    # ------------------------------------------------------------------

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
                log.info("API 키 저장됨 (길이=%d자)", len(new_key))
                rumps.notification("voice-stt", "", "API Key가 저장되었습니다.")
            else:
                log.warning("API Key 입력 없음 — 저장 안 됨")
                rumps.alert(title="voice-stt", message="API Key를 입력해 주세요.")
        else:
            log.debug("API Key 설정 창 취소됨")

    # ------------------------------------------------------------------
    # 설정 편집
    # ------------------------------------------------------------------

    def _on_edit_config(self, _):
        log.info("설정 편집 창 열기")
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                current = f.read()
            log.debug("현재 config.json 로드 완료 (%d bytes)", len(current))
        except Exception:
            log.exception("config.json 로드 실패 — 빈 JSON 사용")
            current = "{}"

        window = rumps.Window(
            title="설정 (config.json)",
            message="JSON을 수정한 후 저장하세요.",
            default_text=current,
            ok="저장",
            cancel="취소",
            dimensions=(400, 180),
        )
        response = window.run()
        if response.clicked:
            text = response.text.strip()
            try:
                parsed = json.loads(text)
                log.debug("JSON 파싱 완료 — 키: %s", list(parsed.keys()))
            except json.JSONDecodeError as e:
                log.warning("JSON 파싱 오류: %s", e)
                rumps.alert(title="voice-stt", message=f"JSON 오류:\n{e}")
                return
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(parsed, f, ensure_ascii=False, indent=2)
            log.info("config.json 저장됨")
            rumps.notification("voice-stt", "", "설정이 저장되었습니다.")
        else:
            log.debug("설정 편집 창 취소됨")

    # ------------------------------------------------------------------
    # UI 큐
    # ------------------------------------------------------------------

    @rumps.timer(0.3)
    def _init_overlay_once(self, sender):
        sender.stop()
        log.info("오버레이 초기화 시작 — 메인 스레드에서 실행")
        self.overlay = RecordingOverlay()
        log.info("오버레이 초기화 완료 — available=%s", self.overlay._available)

    def _ui(self, fn):
        qsize_before = self._ui_queue.qsize()
        self._ui_queue.put(fn)
        log.debug("UI 큐 추가 — 큐 크기: %d → %d", qsize_before, qsize_before + 1)

    @rumps.timer(0.05)
    def _flush_ui_queue(self, _):
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
                log.exception("UI 큐 콜백 오류 — 계속 진행")
        if processed > 0:
            log.debug("UI 큐 처리 완료 — %d개 항목 실행", processed)
        if self.overlay and (self.recording or self._transcribing):
            self.overlay.tick()

    # ------------------------------------------------------------------
    # 키 이벤트
    # ------------------------------------------------------------------

    def _on_press(self, key):
        log.debug("키 눌림: %r", key)
        if key == keyboard.Key.alt_r:
            if self.recording:
                log.debug("Right Option 눌림 — 이미 녹음중 (recording=True), 무시")
            elif self._transcribing:
                log.debug("Right Option 눌림 — 변환중 (transcribing=True), 무시")
            else:
                log.info("Right Option 눌림 — 녹음 시작")
                self._start_recording()
        elif key == keyboard.Key.esc:
            if self.recording or self._transcribing:
                log.info("ESC 눌림 — 녹음 취소 (recording=%s, transcribing=%s)",
                         self.recording, self._transcribing)
                self._cancel_recording()
            else:
                log.debug("ESC 눌림 — 녹음/변환 중 아님, 무시")

    def _on_release(self, key):
        log.debug("키 뗌: %r", key)
        if key == keyboard.Key.alt_r:
            if self.recording:
                log.info("Right Option 뗌 — STT 변환 시작")
                self._stop_and_transcribe()
            else:
                log.debug("Right Option 뗌 — 녹음중 아님 (recording=False, transcribing=%s), 무시",
                          self._transcribing)

    # ------------------------------------------------------------------
    # 녹음
    # ------------------------------------------------------------------

    def _cancel_recording(self):
        log.debug("_cancel_recording 진입 — recording=%s, transcribing=%s, cancelled=%s, stream=%s",
                  self.recording, self._transcribing, self._cancelled, "있음" if self.stream else "없음")
        self._cancelled = True
        self.recording = False
        if self.stream:
            try:
                log.debug("마이크 스트림 중지 시작")
                self.stream.stop()
                log.debug("마이크 스트림 중지 완료")
                self.stream.close()
                log.debug("마이크 스트림 닫기 완료")
            except Exception:
                log.exception("마이크 스트림 중지/닫기 실패")
            self.stream = None
        log.info("녹음 취소됨")
        self._ui(lambda: (
            setattr(self, "title", "🎙"),
            setattr(self.status_item, "title", "상태: 취소됨"),
            self.overlay.hide() if self.overlay else None,
        ))
        if self._reset_timer:
            log.debug("기존 리셋 타이머 취소")
            self._reset_timer.cancel()

        def _on_cancel_timer():
            log.debug("취소 리셋 타이머 발동 — '대기중'으로 복귀")
            self._ui(lambda: setattr(self.status_item, "title", "상태: 대기중"))

        self._reset_timer = threading.Timer(2.5, _on_cancel_timer)
        self._reset_timer.start()
        log.debug("취소 리셋 타이머 시작 (2.5초 후)")

    def _start_recording(self):
        log.debug("_start_recording 진입 — _reset_timer=%s, cancelled=%s",
                  "있음" if self._reset_timer else "없음", self._cancelled)
        if self._reset_timer:
            log.debug("기존 리셋 타이머 취소")
            self._reset_timer.cancel()
            self._reset_timer = None
        self._cancelled = False
        self.recording = True
        self.audio_frames = []
        log.debug("상태 초기화 — recording=True, cancelled=False, audio_frames 초기화")

        self._ui(lambda: (
            setattr(self, "title", "🔴"),
            setattr(self.status_item, "title", "상태: 녹음중..."),
            self.overlay.show("🔴  녹음중") if self.overlay else None,
        ))

        try:
            log.debug("마이크 스트림 생성 중 — samplerate=%d, channels=1, dtype=int16", self.SAMPLE_RATE)
            self.stream = sd.InputStream(
                samplerate=self.SAMPLE_RATE,
                channels=1,
                dtype="int16",
                callback=self._audio_callback,
            )
            self.stream.start()
            log.info("마이크 스트림 시작 완료")
        except Exception as e:
            log.exception("마이크 스트림 생성/시작 실패: [%s] %s", type(e).__name__, e)
            if self.stream:
                try:
                    log.debug("실패한 스트림 닫기 시도")
                    self.stream.close()
                    log.debug("실패한 스트림 닫기 완료")
                except Exception:
                    log.exception("실패한 스트림 닫기도 실패")
            self.stream = None
            self.recording = False
            log.debug("마이크 오류로 recording=False 복귀")
            self._ui(lambda: (
                setattr(self, "title", "🎙"),
                setattr(self.status_item, "title", "상태: 마이크 오류"),
                self.overlay.hide() if self.overlay else None,
            ))
            if self._reset_timer:
                self._reset_timer.cancel()

            def _on_mic_error_timer():
                log.debug("마이크 오류 리셋 타이머 발동 — '대기중'으로 복귀")
                self._ui(lambda: setattr(self.status_item, "title", "상태: 대기중"))

            self._reset_timer = threading.Timer(2.5, _on_mic_error_timer)
            self._reset_timer.start()
            log.debug("마이크 오류 리셋 타이머 시작 (2.5초 후)")

    def _audio_callback(self, indata, frames, time_info, status):
        if status:
            log.warning("오디오 콜백 상태 플래그: %s (frames=%d) — 오버플로/언더플로 가능성", status, frames)
        self.audio_frames.append(indata.copy())
        rms = float(np.sqrt(np.mean(indata.astype(np.float32) ** 2)))
        if self.overlay:
            self.overlay.set_volume(min(1.0, rms / 4000.0))

    def _stop_and_transcribe(self):
        log.debug("_stop_and_transcribe 진입 — frames=%d, cancelled=%s, stream=%s",
                  len(self.audio_frames), self._cancelled, "있음" if self.stream else "없음")
        self.recording = False
        frames_count = len(self.audio_frames)

        self._ui(lambda: (
            setattr(self, "title", "⏳"),
            setattr(self.status_item, "title", "상태: 변환중..."),
            self.overlay.show("⏳  변환중...") if self.overlay else None,
        ))

        if self.stream:
            try:
                log.debug("마이크 스트림 중지 시작 (STT 준비)")
                self.stream.stop()
                log.debug("마이크 스트림 중지 완료")
                self.stream.close()
                log.debug("마이크 스트림 닫기 완료")
            except Exception:
                log.exception("마이크 스트림 중지/닫기 실패 (STT 준비 중)")
            self.stream = None

        log.info("녹음 종료 — %d 청크 수집", frames_count)

        if self._cancelled:
            log.debug("_cancelled=True — STT 스레드 시작 생략, 조기 반환")
            return

        log.debug("_transcribing=True 설정, STT 스레드 시작")
        self._transcribing = True
        t = threading.Thread(target=self._transcribe, daemon=True)
        t.start()
        log.debug("STT 스레드 시작됨 — thread_id=%d", t.ident)

    # ------------------------------------------------------------------
    # STT
    # ------------------------------------------------------------------

    def _transcribe(self):
        thread_id = threading.current_thread().ident
        log.debug("_transcribe 진입 — thread_id=%d", thread_id)
        t_start = time.time()
        tmp_path = None
        error_occurred = False
        try:
            log.debug("config.json 로드 시작")
            config = load_config()
            log.debug("config.json 로드 완료 — language='%s', keyterms=%s",
                      config.get("language"), config.get("keyterms"))

            api_key = self._api_key
            if not api_key:
                log.warning("API Key 미설정 — 변환 불가")
                raise ValueError("API Key가 설정되지 않았습니다.")
            log.debug("API Key 확인 완료")

            if self._cancelled:
                log.debug("취소 확인 (config 로드 후) — 조기 종료")
                return

            if not self.audio_frames:
                log.warning("오디오 프레임 없음 — 변환 불가")
                raise ValueError("녹음된 오디오 데이터가 없습니다.")

            frame_count = len(self.audio_frames)
            log.debug("오디오 프레임 수: %d", frame_count)
            audio_data = np.concatenate(self.audio_frames, axis=0)
            duration_sec = len(audio_data) / self.SAMPLE_RATE
            audio_bytes = len(audio_data) * 2  # int16 = 2 bytes/sample
            log.info("STT 변환 시작 — 오디오 길이 %.1f초 (%d 샘플, %d bytes, %d 청크)",
                     duration_sec, len(audio_data), audio_bytes, frame_count)

            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                tmp_path = f.name
            log.debug("임시 WAV 파일 경로: %s", tmp_path)

            with wave.open(tmp_path, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(self.SAMPLE_RATE)
                wf.writeframes(audio_data.tobytes())
            wav_size = os.path.getsize(tmp_path)
            log.debug("WAV 파일 저장 완료 — 크기=%d bytes", wav_size)

            result = None
            last_exc = None
            for attempt in range(1, 3):
                if self._cancelled:
                    log.debug("취소 확인 — API 시도 %d 이전 조기 종료", attempt)
                    return
                try:
                    log.info("ElevenLabs API 호출 (시도 %d/2)", attempt)
                    log.debug("API 파라미터 — model=scribe_v2, language='%s', keyterms=%s, sdk_timeout=8.0s, hard_timeout=10s",
                              config.get("language", "ko"), config.get("keyterms", []))
                    client = ElevenLabs(api_key=api_key, timeout=8.0)

                    _holder: dict = {}

                    def _call_api(_holder=_holder):
                        inner_tid = threading.current_thread().ident
                        log.debug("API 호출 스레드 진입 — thread_id=%d", inner_tid)
                        try:
                            with open(tmp_path, "rb") as f:
                                log.debug("API 요청 전송 중 (thread_id=%d)...", inner_tid)
                                _holder["result"] = client.speech_to_text.convert(
                                    file=f,
                                    model_id="scribe_v2",
                                    language_code=config.get("language", "ko"),
                                    keyterms=config.get("keyterms", []) or None,
                                )
                            log.debug("API 호출 스레드 완료 — 결과 수신 (thread_id=%d)", inner_tid)
                        except BaseException as _exc:
                            log.debug("API 호출 스레드 예외: [%s] %s (thread_id=%d)",
                                      type(_exc).__name__, _exc, inner_tid)
                            _holder["error"] = _exc

                    _t = threading.Thread(target=_call_api, daemon=True)
                    _t.start()
                    log.debug("API 스레드 시작됨 — thread_id=%d, join 대기 최대 10s", _t.ident)
                    join_start = time.time()
                    _t.join(timeout=10)
                    join_elapsed = time.time() - join_start
                    log.debug("API 스레드 join 완료 — 경과=%.2f초, alive=%s", join_elapsed, _t.is_alive())

                    if _t.is_alive():
                        log.warning("API 스레드가 10초 후에도 응답 없음 — 하드 타임아웃 발동 (thread_id=%d)", _t.ident)
                        raise TimeoutError("ElevenLabs API 응답 없음 (10초 초과)")

                    if "error" in _holder:
                        log.debug("API 스레드에서 예외 전파: [%s]", type(_holder["error"]).__name__)
                        raise _holder["error"]

                    result = _holder["result"]
                    last_exc = None
                    log.info("API 응답 수신 완료 (시도 %d/2, 소요=%.2f초)", attempt, join_elapsed)
                    log.debug("API 응답 타입: %s, text 길이: %d자",
                              type(result).__name__,
                              len(result.text or "") if result else 0)
                    break

                except Exception as e:
                    last_exc = e
                    log.warning("API 오류 (시도 %d/2): [%s] %s", attempt, type(e).__name__, e)
                    if attempt == 1 and not self._cancelled:
                        log.debug("1초 후 재시도 예정 (cancelled=%s)", self._cancelled)
                        self._ui(lambda: (
                            setattr(self.status_item, "title", "상태: 재시도중..."),
                            self.overlay.show("🔄  재시도중...") if self.overlay else None,
                        ))
                        time.sleep(1.0)
                        log.debug("재시도 대기 완료")

            if last_exc is not None:
                log.debug("모든 시도 실패 — 최종 예외 raise: [%s] %s", type(last_exc).__name__, last_exc)
                raise last_exc

            if self._cancelled:
                log.debug("취소 확인 (API 성공 후) — 붙여넣기 생략, 조기 종료")
                return

            text = (result.text or "").strip()
            log.info("변환 결과: %d자", len(text))
            log.debug("변환 결과 미리보기: %r", text[:80])

            if text:
                preview = text[:40] + ("..." if len(text) > 40 else "")
                log.debug("붙여넣기 UI 큐 등록 — preview='%s'", preview)
                self._ui(lambda t=text, p=preview: (
                    self._paste_text(t),
                    setattr(self.last_item, "title", f"마지막 변환: {p}"),
                    setattr(self.status_item, "title", "상태: 대기중"),
                ))
            else:
                log.warning("변환 결과 텍스트 없음 — API 응답은 있으나 text 필드 비어있음")
                self._ui(lambda: setattr(self.status_item, "title", "상태: 텍스트 없음"))

        except Exception as e:
            error_occurred = True
            log.exception("STT 오류: [%s] %s", type(e).__name__, e)
            if not self._cancelled:
                log.debug("오류 UI 표시 — 2.5초 후 자동 복귀 타이머 시작")
                self._ui(lambda: (
                    setattr(self.status_item, "title", "상태: 실패"),
                    self.overlay.show("❌  실패했습니다") if self.overlay else None,
                ))
                if self._reset_timer:
                    log.debug("기존 리셋 타이머 취소")
                    self._reset_timer.cancel()

                def _on_error_timer():
                    log.debug("오류 리셋 타이머 발동 — '대기중'으로 복귀, 오버레이 숨김")
                    self._ui(lambda: (
                        setattr(self.status_item, "title", "상태: 대기중"),
                        self.overlay.hide() if self.overlay else None,
                    ))

                self._reset_timer = threading.Timer(2.5, _on_error_timer)
                self._reset_timer.start()
                log.debug("오류 리셋 타이머 시작 (2.5초 후)")
            else:
                log.debug("취소 상태에서 오류 발생 — UI 업데이트 생략")

        finally:
            elapsed = time.time() - t_start
            log.debug("_transcribe finally 진입 — 총 소요=%.2f초, error_occurred=%s, cancelled=%s",
                      elapsed, error_occurred, self._cancelled)
            self._transcribing = False
            log.debug("_transcribing=False 설정 완료")

            if tmp_path:
                if os.path.exists(tmp_path):
                    try:
                        os.unlink(tmp_path)
                        log.debug("임시 파일 삭제 완료: %s", tmp_path)
                    except Exception:
                        log.exception("임시 파일 삭제 실패: %s", tmp_path)
                else:
                    log.debug("임시 파일 이미 없음: %s", tmp_path)
            else:
                log.debug("임시 파일 경로 없음 (파일 생성 전 실패)")

            if not self._cancelled:
                log.debug("타이틀 '🎙' 복귀 큐 등록")
                self._ui(lambda: setattr(self, "title", "🎙"))
                if not error_occurred:
                    log.debug("오버레이 숨김 큐 등록 (정상 완료)")
                    self._ui(lambda: self.overlay.hide() if self.overlay else None)
                else:
                    log.debug("오류 발생 — 오버레이 숨김은 리셋 타이머에서 처리")
            else:
                log.debug("취소 상태 — 타이틀/오버레이 복귀 생략 (_cancel_recording에서 처리됨)")

            log.debug("_transcribe 종료 — thread_id=%d, 총 소요=%.2f초", thread_id, elapsed)

    # ------------------------------------------------------------------
    # 붙여넣기 + Enter
    # ------------------------------------------------------------------

    def _paste_text(self, text: str):
        log.debug("_paste_text 진입 — 텍스트 길이=%d자, 미리보기=%r", len(text), text[:40])

        log.debug("pyperclip.copy 시작 (macOS: pbcopy 호출)")
        t_copy = time.time()
        pyperclip.copy(text)
        log.info("클립보드 복사 완료 (소요=%.3f초)", time.time() - t_copy)

        log.debug("keyboard.Controller 생성")
        kb = keyboard.Controller()

        log.debug("Cmd+V 전송 시작")
        with kb.pressed(keyboard.Key.cmd):
            kb.press("v")
            kb.release("v")
        log.debug("Cmd+V 전송 완료")

        log.debug("붙여넣기 후 sleep(0.1) 시작")
        time.sleep(0.1)
        log.debug("sleep(0.1) 완료")

        log.debug("Enter 키 전송 시작")
        kb.press(keyboard.Key.enter)
        kb.release(keyboard.Key.enter)
        log.info("붙여넣기 + Enter 완료")


if __name__ == "__main__":
    log.info("run() 호출")
    VoiceSTTApp().run()
    log.info("run() 종료")
