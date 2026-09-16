# KeyScribe

macOS 메뉴바 앱. 단축키를 누르는 동안 마이크로 녹음하고, 떼면 음성을 텍스트로 변환해 현재 입력창에 붙여넣는다.

## 실행 방법

- **개발 중 수정 후 재실행**: `bash scripts/rebuild_macos_app.sh`
- **로그인 시 앱 실행**: `bash scripts/install_macos_login_app.sh`로 한 번 설치
- **macOS 앱 빌드**: `bash native/macos/build.sh` → `dist-native/KeyScribe.app` 생성

## 로그

- 로그인 앱 로그: `dist-native/app.log`
- 앱 상태와 오류: 메뉴바의 KeyScribe 메뉴에서 확인

## 주의사항

- `.env` 파일은 더 이상 사용하지 않는다.
- `~/Library/Application Support/keyscribe/user_config.json` 파일은 절대 읽지 말 것. API 키 등 민감한 정보가 저장된다.
- `config.toml`에는 민감한 값이 없으므로 읽어도 무방하다.
