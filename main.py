import os
import json
import math
import wave
import time
import queue
import threading
import tempfile
import ctypes
import ctypes.util
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env"))

# 빌드된 앱(.app)에서는 ssl이 CA 파일을 찾지 못한다.
# certifi 인증서를 파일 경로 대신 문자열(cadata)로 직접 주입해 경로 문제를 우회한다.
import ssl
import certifi as _certifi_mod
with open(_certifi_mod.where(), "r", encoding="ascii") as _f:
    _CA_DATA = _f.read()
_orig_create_ssl_context = ssl.create_default_context
def _certifi_ssl_context(purpose=ssl.Purpose.SERVER_AUTH, *, cafile=None, capath=None, cadata=None):
    if cadata is None:
        cadata = _CA_DATA
        cafile = None
        capath = None
    return _orig_create_ssl_context(purpose, cafile=cafile, capath=capath, cadata=cadata)
ssl.create_default_context = _certifi_ssl_context

import numpy as np
import pyperclip
import rumps
import sounddevice as sd
from elevenlabs.client import ElevenLabs
from pynput import keyboard

CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
USER_CONFIG_PATH = Path.home() / "Library" / "Application Support" / "voice-stt" / "user_config.json"


def is_accessibility_granted() -> bool:
    lib = ctypes.cdll.LoadLibrary(ctypes.util.find_library("ApplicationServices"))
    lib.AXIsProcessTrusted.restype = ctypes.c_bool
    return lib.AXIsProcessTrusted()


def load_config() -> dict:
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def load_user_config() -> dict:
    if USER_CONFIG_PATH.exists():
        with open(USER_CONFIG_PATH, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}


def save_user_config(data: dict):
    USER_CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(USER_CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


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
            # 상수 이름이 macOS 버전마다 다름 — 순서대로 시도
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

            # 해상도에 따라 중앙 하단 배치 (Dock 위)
            screen = NSScreen.mainScreen()
            vis = screen.visibleFrame()
            full = screen.frame()
            x = (full.size.width - self.WIDTH) / 2
            y = vis.origin.y + 40

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

            # 볼륨 인디케이터 바
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
            except Exception:
                pass  # 바 없이도 동작

            self._available = True
        except Exception:
            import traceback
            traceback.print_exc()
            print("[voice-stt] 오버레이 초기화 실패 — 오버레이 없이 계속 실행합니다.")
            self._available = False

    def set_volume(self, level: float):
        self._volume = max(0.0, min(1.0, level))

    def tick(self):
        if not self._available or not self._bar_layers:
            return
        self._phase += 0.4
        self._volume *= 0.88  # 자연스러운 감쇠
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
            pass

    def show(self, text: str = "🔴  녹음중"):
        if self._available:
            self._label.setStringValue_(text)
            from AppKit import NSScreen, NSMakeRect
            screen = NSScreen.mainScreen()
            full = screen.frame()
            vis = screen.visibleFrame()
            x = (full.size.width - self.WIDTH) / 2
            y = vis.origin.y + 40
            cur = self._win.frame()
            self._win.setFrame_display_(NSMakeRect(x, y, cur.size.width, cur.size.height), False)
            self._win.orderFrontRegardless()

    def hide(self):
        if self._available:
            self._volume = 0.0
            self._win.orderOut_(None)


class VoiceSTTApp(rumps.App):
    SAMPLE_RATE = 16000

    def __init__(self):
        super().__init__("🎙", quit_button="종료")
        self.recording = False
        self._transcribing = False
        self._cancelled = False
        self._reset_timer: threading.Timer | None = None
        self.audio_frames: list[np.ndarray] = []
        self.stream: sd.InputStream | None = None

        # API 키: 우선순위 — 사용자 설정 > .env 환경변수 > config.json
        user_cfg = load_user_config()
        self._api_key: str = (
            user_cfg.get("api_key")
            or os.environ.get("ELEVENLABS_API_KEY")
            or load_config().get("api_key")
            or ""
        )

        self.status_item = rumps.MenuItem("상태: 대기중")
        self.last_item = rumps.MenuItem("마지막 변환: -")
        self.apikey_item = rumps.MenuItem("API Key 설정...", callback=self._on_set_api_key)
        self.config_item = rumps.MenuItem("설정...", callback=self._on_edit_config)
        self.restart_item = rumps.MenuItem("재실행", callback=self._restart)

        if not is_accessibility_granted():
            self._accessibility_item = rumps.MenuItem(
                "⚠️ 접근성 권한 필요 — 클릭하여 설정 열기",
                callback=self._open_accessibility_prefs,
            )
            self.menu = [self.status_item, self.last_item, None, self._accessibility_item, None, self.apikey_item, self.config_item, None, self.restart_item, None]
        else:
            self._accessibility_item = None
            self.menu = [self.status_item, self.last_item, None, self.apikey_item, self.config_item, None, self.restart_item, None]

        self.overlay: RecordingOverlay | None = None  # 런루프 시작 후 초기화
        self._ui_queue: queue.Queue = queue.Queue()

        listener = keyboard.Listener(
            on_press=self._on_press,
            on_release=self._on_release,
        )
        listener.daemon = True
        listener.start()

    # ------------------------------------------------------------------
    # 접근성 권한 안내
    # ------------------------------------------------------------------

    def _open_accessibility_prefs(self, _):
        import subprocess
        subprocess.Popen([
            "open",
            "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility",
        ])
        rumps.notification("voice-stt", "", "권한 허용 후 앱을 재시작해 주세요.")

    def _restart(self, _):
        import subprocess
        import sys
        exe = sys.executable
        if ".app/Contents" in exe:
            bundle = exe[:exe.index(".app/Contents") + 4]
            subprocess.Popen(["open", bundle])
        else:
            subprocess.Popen([sys.executable] + sys.argv)
        rumps.quit_application()

    # ------------------------------------------------------------------
    # API Key 설정 다이얼로그 (메인 스레드, 메뉴 클릭 콜백)
    # ------------------------------------------------------------------

    def _on_set_api_key(self, _):
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
                rumps.notification("voice-stt", "", "API Key가 저장되었습니다.")
            else:
                rumps.alert(title="voice-stt", message="API Key를 입력해 주세요.")

    # ------------------------------------------------------------------
    # 설정 편집 (config.json 직접 수정)
    # ------------------------------------------------------------------

    def _on_edit_config(self, _):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                current = f.read()
        except Exception:
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
            except json.JSONDecodeError as e:
                rumps.alert(title="voice-stt", message=f"JSON 오류:\n{e}")
                return
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(parsed, f, ensure_ascii=False, indent=2)
            rumps.notification("voice-stt", "", "설정이 저장되었습니다.")

    # ------------------------------------------------------------------
    # UI 큐 (백그라운드 → 메인 스레드)
    # ------------------------------------------------------------------

    @rumps.timer(0.3)
    def _init_overlay_once(self, sender):
        sender.stop()
        self.overlay = RecordingOverlay()

    def _ui(self, fn):
        self._ui_queue.put(fn)

    @rumps.timer(0.05)
    def _flush_ui_queue(self, _):
        try:
            while True:
                fn = self._ui_queue.get_nowait()
                fn()
        except queue.Empty:
            pass
        if self.overlay and (self.recording or self._transcribing):
            self.overlay.tick()

    # ------------------------------------------------------------------
    # 키 이벤트 (pynput 백그라운드 스레드)
    # ------------------------------------------------------------------

    def _on_press(self, key):
        if key == keyboard.Key.alt_r and not self.recording and not self._transcribing:
            self._start_recording()
        elif key == keyboard.Key.esc and (self.recording or self._transcribing):
            self._cancel_recording()

    def _on_release(self, key):
        if key == keyboard.Key.alt_r and self.recording:
            self._stop_and_transcribe()

    # ------------------------------------------------------------------
    # 녹음 (pynput 백그라운드 스레드에서 호출됨)
    # ------------------------------------------------------------------

    def _cancel_recording(self):
        self._cancelled = True
        self.recording = False
        if self.stream:
            self.stream.stop()
            self.stream.close()
            self.stream = None
        self._ui(lambda: (
            setattr(self, "title", "🎙"),
            setattr(self.status_item, "title", "상태: 취소됨"),
            self.overlay.hide() if self.overlay else None,
        ))
        if self._reset_timer:
            self._reset_timer.cancel()
        self._reset_timer = threading.Timer(
            2.5,
            lambda: self._ui(lambda: setattr(self.status_item, "title", "상태: 대기중")),
        )
        self._reset_timer.start()

    def _start_recording(self):
        if self._reset_timer:
            self._reset_timer.cancel()
            self._reset_timer = None
        self._cancelled = False
        self.recording = True
        self.audio_frames = []

        self._ui(lambda: (
            setattr(self, "title", "🔴"),
            setattr(self.status_item, "title", "상태: 녹음중..."),
            self.overlay.show("🔴  녹음중") if self.overlay else None,
        ))

        try:
            self.stream = sd.InputStream(
                samplerate=self.SAMPLE_RATE,
                channels=1,
                dtype="int16",
                callback=self._audio_callback,
            )
            self.stream.start()
        except Exception as e:
            print(f"[voice-stt] 마이크 오류: {e}", flush=True)
            if self.stream:
                try:
                    self.stream.close()
                except Exception:
                    pass
            self.stream = None
            self.recording = False
            self._ui(lambda: (
                setattr(self, "title", "🎙"),
                setattr(self.status_item, "title", "상태: 마이크 오류"),
                self.overlay.hide() if self.overlay else None,
            ))
            if self._reset_timer:
                self._reset_timer.cancel()
            self._reset_timer = threading.Timer(
                2.5,
                lambda: self._ui(lambda: setattr(self.status_item, "title", "상태: 대기중")),
            )
            self._reset_timer.start()

    def _audio_callback(self, indata, frames, time_info, status):
        self.audio_frames.append(indata.copy())
        rms = float(np.sqrt(np.mean(indata.astype(np.float32) ** 2)))
        if self.overlay:
            self.overlay.set_volume(min(1.0, rms / 4000.0))

    def _stop_and_transcribe(self):
        self.recording = False

        self._ui(lambda: (
            setattr(self, "title", "⏳"),
            setattr(self.status_item, "title", "상태: 변환중..."),
            self.overlay.show("⏳  변환중...") if self.overlay else None,
        ))

        if self.stream:
            self.stream.stop()
            self.stream.close()
            self.stream = None

        if self._cancelled:
            return
        self._transcribing = True
        threading.Thread(target=self._transcribe, daemon=True).start()

    # ------------------------------------------------------------------
    # STT (별도 백그라운드 스레드)
    # ------------------------------------------------------------------

    def _transcribe(self):
        tmp_path = None
        error_occurred = False
        try:
            config = load_config()

            api_key = self._api_key
            if not api_key:
                raise ValueError("API Key가 설정되지 않았습니다. 메뉴에서 'API Key 설정...'을 눌러 입력해 주세요.")

            if self._cancelled:
                return

            if not self.audio_frames:
                raise ValueError("녹음된 오디오 데이터가 없습니다.")

            audio_data = np.concatenate(self.audio_frames, axis=0)
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                tmp_path = f.name

            with wave.open(tmp_path, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(self.SAMPLE_RATE)
                wf.writeframes(audio_data.tobytes())

            result = None
            last_exc = None
            for attempt in range(1, 3):
                if self._cancelled:
                    return
                try:
                    client = ElevenLabs(api_key=api_key, timeout=5.0)
                    with open(tmp_path, "rb") as f:
                        result = client.speech_to_text.convert(
                            file=f,
                            model_id="scribe_v2",
                            language_code=config.get("language", "ko"),
                            keyterms=config.get("keyterms", []) or None,
                        )
                    last_exc = None
                    break
                except Exception as e:
                    last_exc = e
                    print(f"[voice-stt] API 오류 (시도 {attempt}/2): {e}", flush=True)
                    if attempt == 1 and not self._cancelled:
                        self._ui(lambda: (
                            setattr(self.status_item, "title", "상태: 재시도중..."),
                            self.overlay.show("🔄  재시도중...") if self.overlay else None,
                        ))
                        time.sleep(1.0)

            if last_exc is not None:
                raise last_exc

            if self._cancelled:
                return

            text = (result.text or "").strip()
            if text:
                preview = text[:40] + ("..." if len(text) > 40 else "")
                self._ui(lambda t=text, p=preview: (
                    self._paste_text(t),
                    setattr(self.last_item, "title", f"마지막 변환: {p}"),
                    setattr(self.status_item, "title", "상태: 대기중"),
                ))
            else:
                self._ui(lambda: setattr(self.status_item, "title", "상태: 텍스트 없음"))

        except Exception as e:
            error_occurred = True
            import traceback
            traceback.print_exc()
            print(f"[voice-stt] 오류: {e}", flush=True)
            if not self._cancelled:
                self._ui(lambda: (
                    setattr(self.status_item, "title", "상태: 실패"),
                    self.overlay.show("❌  실패했습니다") if self.overlay else None,
                ))
                if self._reset_timer:
                    self._reset_timer.cancel()
                self._reset_timer = threading.Timer(
                    2.5,
                    lambda: self._ui(lambda: (
                        setattr(self.status_item, "title", "상태: 대기중"),
                        self.overlay.hide() if self.overlay else None,
                    )),
                )
                self._reset_timer.start()

        finally:
            self._transcribing = False
            if tmp_path and os.path.exists(tmp_path):
                os.unlink(tmp_path)
            if not self._cancelled:
                self._ui(lambda: setattr(self, "title", "🎙"))
                if not error_occurred:
                    self._ui(lambda: self.overlay.hide() if self.overlay else None)

    # ------------------------------------------------------------------
    # 붙여넣기 + Enter (메인 스레드에서 호출됨)
    # ------------------------------------------------------------------

    def _paste_text(self, text: str):
        pyperclip.copy(text)
        kb = keyboard.Controller()

        with kb.pressed(keyboard.Key.cmd):
            kb.press("v")
            kb.release("v")

        time.sleep(0.1)

        kb.press(keyboard.Key.enter)
        kb.release(keyboard.Key.enter)


if __name__ == "__main__":
    VoiceSTTApp().run()
