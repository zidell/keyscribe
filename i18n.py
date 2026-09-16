"""KeyScribe UI strings. Unsupported UI locales intentionally fall back to English."""

_ui_language = "ko"

MESSAGES = {
    "en": {
        "status_idle": "Status: Idle", "status_recording": "Status: Recording…", "status_transcribing": "Status: Transcribing…",
        "status_retrying": "Status: Retrying…", "status_cancelled": "Status: Cancelled", "status_mic_error": "Status: Microphone error",
        "status_no_text": "Status: No text", "status_failed": "Status: Failed", "last": "Last transcription: {text}",
        "settings": "Settings…", "restart": "Restart", "quit": "Quit", "api_key": "API Key", "model": "Model",
        "refresh": "Refresh", "shortcut": "Recording shortcut", "recording_control": "Recording control",
        "recording_control_hold": "Stop recording when key is released", "recording_control_toggle": "Toggle to start and stop",
        "auto_send": "Automatically send after pasting",
        "ui_language": "App language", "speech_language": "Transcription language", "keywords": "Recognition keywords\n(one per line)",
        "remove_fillers": "Remove filler words automatically (ElevenLabs only)", "filler_hint": "(um, uh, etc.)", "mute_audio": "Mute system audio while recording (macOS only)",
        "save": "Save", "cancel": "Cancel", "loading": "Loading…", "saved": "Settings saved.",
        "settings_title": "{app} Settings", "api_key_help": "The API Key is stored only on this computer.",
        "api_hint": "(ElevenLabs / OpenAI)", "api_key_error": "API Key error — enter the correct API Key.",
        "api_key_error_title": "Check API Key", "api_key_error_detail": "There is an API Key error. Please enter the correct API Key exactly.\n\n{error}",
        "no_models": "No transcription models found.", "accessibility_required": "⚠️ Accessibility permission required — click to open settings",
        "permission_restart": "Allow permission, then restart the app.", "overlay_recording": "🔴  Recording", "overlay_transcribing": "⏳  Transcribing…",
        "windows_mic_help": "The microphone could not be opened. Check that it is connected and that Windows allows desktop apps to use it. Open microphone settings?",
        "overlay_no_mic": "❌  No microphone", "overlay_failed": "❌  Failed", "overlay_retrying": "🔄  Retrying…",
    },
    "ko": {
        "status_idle": "상태: 대기중", "status_recording": "상태: 녹음중…", "status_transcribing": "상태: 변환중…",
        "status_retrying": "상태: 재시도중…", "status_cancelled": "상태: 취소됨", "status_mic_error": "상태: 마이크 오류",
        "status_no_text": "상태: 텍스트 없음", "status_failed": "상태: 실패", "last": "마지막 변환: {text}",
        "settings": "설정…", "restart": "재실행", "quit": "종료", "api_key": "API Key", "model": "사용 모델",
        "refresh": "새로고침", "shortcut": "녹음 단축키", "recording_control": "녹음제어 방식",
        "recording_control_hold": "키를 땔 떼 녹음도 종료", "recording_control_toggle": "토글로 시작과 종료",
        "auto_send": "붙여넣기 후 자동 전송",
        "ui_language": "앱 언어", "speech_language": "음성 인식 언어", "keywords": "인식 키워드\n(한 줄에 하나)",
        "remove_fillers": "필러 단어 자동 제거 (ElevenLabs 전용)", "filler_hint": "(um, uh, 어, 음 등)", "mute_audio": "녹음 중 시스템 오디오 음소거 (macOS만)",
        "save": "저장", "cancel": "취소", "loading": "불러오는 중…", "saved": "설정이 저장되었습니다.",
        "settings_title": "{app} 설정", "api_key_help": "API Key는 이 컴퓨터에만 저장됩니다.", "api_hint": "(ElevenLabs / OpenAI)",
        "api_key_error": "API Key 오류 — 올바른 API Key를 정확히 입력해 주세요.", "api_key_error_title": "API Key 확인 필요",
        "api_key_error_detail": "API Key 오류가 있습니다. 올바른 API Key를 정확히 입력해 주세요.\n\n{error}",
        "no_models": "전사 가능한 모델을 찾을 수 없습니다.", "accessibility_required": "⚠️ 접근성 권한 필요 — 클릭하여 설정 열기",
        "permission_restart": "권한 허용 후 앱을 재시작해 주세요.", "overlay_recording": "🔴  녹음중", "overlay_transcribing": "⏳  변환중…",
        "windows_mic_help": "마이크를 열 수 없습니다. 마이크 연결과 Windows의 데스크톱 앱 마이크 접근 허용 설정을 확인해 주세요. 마이크 설정을 열까요?",
        "overlay_no_mic": "❌  마이크 없음", "overlay_failed": "❌  실패했습니다", "overlay_retrying": "🔄  재시도중…",
    },
    "ja": {
        "windows_mic_help": "マイクを開けません。接続と、Windowsでデスクトップアプリのマイク使用が許可されているか確認してください。マイクの設定を開きますか？",
        "recording_control": "録音の操作方法", "recording_control_hold": "キーを離したら録音を終了",
        "recording_control_toggle": "押すたびに録音を開始・終了",
        "status_idle": "状態: 待機中", "status_recording": "状態: 録音中…", "status_transcribing": "状態: 文字起こし中…", "status_retrying": "状態: 再試行中…", "status_cancelled": "状態: キャンセル済み", "status_mic_error": "状態: マイクエラー", "status_no_text": "状態: テキストなし", "status_failed": "状態: 失敗", "last": "最後の文字起こし: {text}", "settings": "設定…", "restart": "再起動", "quit": "終了", "api_key": "API Key", "model": "使用モデル", "refresh": "更新", "shortcut": "録音ショートカット", "auto_send": "貼り付け後に自動送信", "ui_language": "アプリの言語", "speech_language": "文字起こしの言語", "keywords": "認識キーワード\n(1行に1つ)", "remove_fillers": "フィラーを自動削除（ElevenLabsのみ）", "filler_hint": "（um、uh など）", "mute_audio": "録音中はシステム音声をミュート (macOSのみ)", "save": "保存", "cancel": "キャンセル", "loading": "読み込み中…", "saved": "設定を保存しました。", "settings_title": "{app} 設定", "api_key_help": "API Keyはこのコンピュータにのみ保存されます。", "api_hint": "(ElevenLabs / OpenAI)", "api_key_error": "API Key エラー — 正しい API Key を入力してください。", "api_key_error_title": "API Key の確認", "api_key_error_detail": "API Key にエラーがあります。正しい API Key を正確に入力してください。\n\n{error}", "no_models": "文字起こしモデルが見つかりません。", "accessibility_required": "⚠️ アクセシビリティの許可が必要 — クリックして設定を開く", "permission_restart": "許可後にアプリを再起動してください。", "overlay_recording": "🔴  録音中", "overlay_transcribing": "⏳  文字起こし中…", "overlay_no_mic": "❌  マイクなし", "overlay_failed": "❌  失敗しました", "overlay_retrying": "🔄  再試行中…",
    },
    "zh-Hans": {
        "windows_mic_help": "无法打开麦克风。请检查设备连接，以及 Windows 是否允许桌面应用使用麦克风。要打开麦克风设置吗？",
        "recording_control": "录音控制方式", "recording_control_hold": "松开按键时结束录音",
        "recording_control_toggle": "按键切换开始和结束",
        "status_idle": "状态：空闲", "status_recording": "状态：录音中…", "status_transcribing": "状态：转写中…", "status_retrying": "状态：重试中…", "status_cancelled": "状态：已取消", "status_mic_error": "状态：麦克风错误", "status_no_text": "状态：无文本", "status_failed": "状态：失败", "last": "最近转写：{text}", "settings": "设置…", "restart": "重新启动", "quit": "退出", "api_key": "API Key", "model": "所用模型", "refresh": "刷新", "shortcut": "录音快捷键", "auto_send": "粘贴后自动发送", "ui_language": "应用语言", "speech_language": "转写语言", "keywords": "识别关键词\n(每行一个)", "remove_fillers": "自动删除填充词（仅 ElevenLabs）", "filler_hint": "（um、uh 等）", "mute_audio": "录音时静音系统音频（仅 macOS）", "save": "保存", "cancel": "取消", "loading": "正在加载…", "saved": "设置已保存。", "settings_title": "{app} 设置", "api_key_help": "API Key 仅保存在此电脑上。", "api_hint": "(ElevenLabs / OpenAI)", "api_key_error": "API Key 错误 — 请输入正确的 API Key。", "api_key_error_title": "检查 API Key", "api_key_error_detail": "API Key 有错误。请准确输入正确的 API Key。\n\n{error}", "no_models": "找不到转写模型。", "accessibility_required": "⚠️ 需要辅助功能权限 — 点击打开设置", "permission_restart": "授予权限后请重新启动应用。", "overlay_recording": "🔴  录音中", "overlay_transcribing": "⏳  转写中…", "overlay_no_mic": "❌  无麦克风", "overlay_failed": "❌  失败", "overlay_retrying": "🔄  重试中…",
    },
    "es": {
        "windows_mic_help": "No se pudo abrir el micrófono. Comprueba la conexión y el acceso al micrófono para aplicaciones de escritorio en Windows. ¿Abrir la configuración del micrófono?",
        "recording_control": "Control de grabación", "recording_control_hold": "Detener al soltar la tecla",
        "recording_control_toggle": "Pulsar para iniciar o detener",
        "status_idle": "Estado: En espera", "status_recording": "Estado: Grabando…", "status_transcribing": "Estado: Transcribiendo…", "status_retrying": "Estado: Reintentando…", "status_cancelled": "Estado: Cancelado", "status_mic_error": "Estado: Error de micrófono", "status_no_text": "Estado: Sin texto", "status_failed": "Estado: Error", "last": "Última transcripción: {text}", "settings": "Configuración…", "restart": "Reiniciar", "quit": "Salir", "api_key": "API Key", "model": "Modelo", "refresh": "Actualizar", "shortcut": "Atajo de grabación", "auto_send": "Enviar automáticamente después de pegar", "ui_language": "Idioma de la aplicación", "speech_language": "Idioma de transcripción", "keywords": "Palabras clave de reconocimiento\n(una por línea)", "remove_fillers": "Eliminar muletillas automáticamente (solo ElevenLabs)", "filler_hint": "(um, uh, etc.)", "mute_audio": "Silenciar el audio del sistema al grabar (solo macOS)", "save": "Guardar", "cancel": "Cancelar", "loading": "Cargando…", "saved": "Configuración guardada.", "settings_title": "Configuración de {app}", "api_key_help": "La API Key se guarda solo en este equipo.", "api_hint": "(ElevenLabs / OpenAI)", "api_key_error": "Error de API Key — introduce la API Key correcta.", "api_key_error_title": "Comprobar API Key", "api_key_error_detail": "Hay un error en la API Key. Introduce exactamente la API Key correcta.\n\n{error}", "no_models": "No se encontraron modelos de transcripción.", "accessibility_required": "⚠️ Se requiere permiso de Accesibilidad — haz clic para abrir Configuración", "permission_restart": "Concede el permiso y reinicia la aplicación.", "overlay_recording": "🔴  Grabando", "overlay_transcribing": "⏳  Transcribiendo…", "overlay_no_mic": "❌  Sin micrófono", "overlay_failed": "❌  Error", "overlay_retrying": "🔄  Reintentando…",
    },
}

def set_ui_language(language: str | None) -> None:
    global _ui_language
    _ui_language = language if language in MESSAGES else "en"

def tr(key: str, **values: object) -> str:
    return MESSAGES.get(_ui_language, MESSAGES["en"]).get(key, MESSAGES["en"].get(key, key)).format(**values)
