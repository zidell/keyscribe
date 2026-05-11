import os
import json
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

import numpy as np
import pyperclip
import rumps
import sounddevice as sd
from elevenlabs.client import ElevenLabs
from pynput import keyboard

CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
USER_CONFIG_PATH = Path.home() / "Library" / "Application Support" / "voice-stt" / "user_config.json"


def ensure_accessibility_permission():
    import subprocess

    lib = ctypes.cdll.LoadLibrary(ctypes.util.find_library("ApplicationServices"))
    lib.AXIsProcessTrusted.restype = ctypes.c_bool

    if lib.AXIsProcessTrusted():
        return

    subprocess.Popen([
        "open",
        "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility",
    ])
    subprocess.run([
        "osascript", "-e",
        'display dialog "voice-stt 가 접근성 권한을 필요로 합니다.\\n\\n'
        "시스템 환경설정 > 개인 정보 보호 및 보안 > 접근성 에서 "
        '이 앱을 허용한 후 다시 실행해 주세요." '
        'buttons {"확인"} default button "확인" with title "voice-stt"',
    ])
    raise SystemExit(0)


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
    WIDTH = 200
    HEIGHT = 48

    def __init__(self):
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
                from AppKit import NSTextAlignmentCenter as _align_center
            except ImportError:
                _align_center = 2

            screen = NSScreen.mainScreen().frame()
            x = (screen.size.width - self.WIDTH) / 2
            y = 80

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
                NSColor.colorWithCalibratedRed_green_blue_alpha_(0.08, 0.08, 0.08, 0.82).CGColor()
            )
            content.layer().setCornerRadius_(14)
            content.layer().setMasksToBounds_(True)

            # 15pt 폰트 렌더링 높이 ≈ 20pt → 박스 높이에서 수직 중앙 정렬
            font_h = 20
            label_y = (self.HEIGHT - font_h) // 2
            self._label = NSTextField.alloc().initWithFrame_(
                NSMakeRect(0, label_y, self.WIDTH, font_h)
            )
            self._label.setEditable_(False)
            self._label.setBezeled_(False)
            self._label.setDrawsBackground_(False)
            self._label.setTextColor_(NSColor.whiteColor())
            self._label.setAlignment_(_align_center)
            self._label.setFont_(NSFont.systemFontOfSize_(15))
            content.addSubview_(self._label)

            self._available = True
        except Exception:
            import traceback
            traceback.print_exc()
            print("[voice-stt] 오버레이 초기화 실패 — 오버레이 없이 계속 실행합니다.")
            self._available = False

    def show(self, text: str = "🔴  녹음중"):
        if self._available:
            self._label.setStringValue_(text)
            self._win.orderFrontRegardless()

    def hide(self):
        if self._available:
            self._win.orderOut_(None)


class VoiceSTTApp(rumps.App):
    SAMPLE_RATE = 16000

    def __init__(self):
        super().__init__("🎙", quit_button="종료")
        self.recording = False
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
        self.menu = [self.status_item, self.last_item, None, self.apikey_item, None]

        self.overlay: RecordingOverlay | None = None  # 런루프 시작 후 초기화
        self._ui_queue: queue.Queue = queue.Queue()

        listener = keyboard.Listener(
            on_press=self._on_press,
            on_release=self._on_release,
        )
        listener.daemon = True
        listener.start()

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

    # ------------------------------------------------------------------
    # 키 이벤트 (pynput 백그라운드 스레드)
    # ------------------------------------------------------------------

    def _on_press(self, key):
        if key == keyboard.Key.alt_r and not self.recording:
            self._start_recording()

    def _on_release(self, key):
        if key == keyboard.Key.alt_r and self.recording:
            self._stop_and_transcribe()

    # ------------------------------------------------------------------
    # 녹음 (pynput 백그라운드 스레드에서 호출됨)
    # ------------------------------------------------------------------

    def _start_recording(self):
        self.recording = True
        self.audio_frames = []

        self._ui(lambda: (
            setattr(self, "title", "🔴"),
            setattr(self.status_item, "title", "상태: 녹음중..."),
            self.overlay.show("🔴  녹음중") if self.overlay else None,
        ))

        self.stream = sd.InputStream(
            samplerate=self.SAMPLE_RATE,
            channels=1,
            dtype="int16",
            callback=self._audio_callback,
        )
        self.stream.start()

    def _audio_callback(self, indata, frames, time_info, status):
        self.audio_frames.append(indata.copy())

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

        threading.Thread(target=self._transcribe, daemon=True).start()

    # ------------------------------------------------------------------
    # STT (별도 백그라운드 스레드)
    # ------------------------------------------------------------------

    def _transcribe(self):
        tmp_path = None
        try:
            config = load_config()

            api_key = self._api_key
            if not api_key:
                raise ValueError("API Key가 설정되지 않았습니다. 메뉴에서 'API Key 설정...'을 눌러 입력해 주세요.")

            audio_data = np.concatenate(self.audio_frames, axis=0)
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                tmp_path = f.name

            with wave.open(tmp_path, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(self.SAMPLE_RATE)
                wf.writeframes(audio_data.tobytes())

            client = ElevenLabs(api_key=api_key)
            with open(tmp_path, "rb") as f:
                result = client.speech_to_text.convert(
                    file=f,
                    model_id="scribe_v2",
                    language_code=config.get("language", "ko"),
                    keyterms=config.get("keyterms", []) or None,
                )

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
            import traceback
            traceback.print_exc()
            print(f"[voice-stt] 오류: {e}", flush=True)
            err = type(e).__name__
            self._ui(lambda err=err: setattr(self.status_item, "title", f"상태: 오류 - {err}"))

        finally:
            if tmp_path and os.path.exists(tmp_path):
                os.unlink(tmp_path)
            self._ui(lambda: (
                setattr(self, "title", "🎙"),
                self.overlay.hide() if self.overlay else None,
            ))

    # ------------------------------------------------------------------
    # 붙여넣기 + Enter (메인 스레드에서 호출됨)
    # ------------------------------------------------------------------

    def _paste_text(self, text: str):
        pyperclip.copy(text)
        kb = keyboard.Controller()

        with kb.pressed(keyboard.Key.cmd):
            kb.press("v")
            kb.release("v")

        time.sleep(0.05)

        kb.press(keyboard.Key.enter)
        kb.release(keyboard.Key.enter)


if __name__ == "__main__":
    ensure_accessibility_permission()
    VoiceSTTApp().run()
