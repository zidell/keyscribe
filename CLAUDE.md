# KeyScribe

macOS 메뉴바 앱. 단축키를 누르는 동안 마이크로 녹음하고, 떼면 음성을 텍스트로 변환해 현재 입력창에 붙여넣는다.

## 실행 방법

- **개발 중 직접 실행**: `python3 main.py`
- **macOS 앱 빌드**: `python3 setup.py py2app` → `dist/KeyScribe.app` 생성

## 로그

- 위치: `~/Library/Logs/keyscribe/keyscribe.log`
- 자정마다 롤오버, 하루치만 보관 (backupCount=1)
- 앱 시작/종료, 모듈 로드, 녹음/STT 흐름, 오류 전체 기록됨
- 문제 발생 시 이 파일 먼저 확인

## 주의사항

- `.env` 파일은 더 이상 사용하지 않는다.
- `~/Library/Application Support/keyscribe/user_config.json` 파일은 절대 읽지 말 것. API 키 등 민감한 정보가 저장된다.
- `config.toml`에는 민감한 값이 없으므로 읽어도 무방하다.
