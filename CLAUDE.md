# voice-stt

macOS 메뉴바 앱. Right Option 키를 누르는 동안 마이크로 녹음하고, 떼면 ElevenLabs Scribe v2로 STT 변환 후 클립보드에 붙여넣는다.

## 주의사항

- `.env` 파일은 더 이상 사용하지 않는다.
- `~/Library/Application Support/voice-stt/user_config.json` 파일은 절대 읽지 말 것. API 키 등 민감한 정보가 저장된다.
- `config.json`에는 민감한 값이 없으므로 읽어도 무방하다.
