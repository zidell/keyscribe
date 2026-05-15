# voice-stt

macOS 메뉴바 앱. Right Option 키를 누르는 동안 마이크로 녹음하고, 떼면 ElevenLabs Scribe v2로 STT 변환 후 클립보드에 붙여넣는다.

## 실행 방법

- **개발 중 직접 실행**: `python3 main.py`
- **macOS 앱 빌드**: `python3 setup.py py2app` → `dist/voice-stt.app` 생성

## 로그

- 위치: `~/Library/Logs/voice-stt/voice-stt.log`
- 자정마다 롤오버, 하루치만 보관 (backupCount=1)
- 앱 시작/종료, 모듈 로드, 녹음/STT 흐름, 오류 전체 기록됨
- 문제 발생 시 이 파일 먼저 확인

## 주의사항

- `.env` 파일은 더 이상 사용하지 않는다.
- `~/Library/Application Support/voice-stt/user_config.json` 파일은 절대 읽지 말 것. API 키 등 민감한 정보가 저장된다.
- `config.toml`에는 민감한 값이 없으므로 읽어도 무방하다.
