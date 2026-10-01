# 개인정보 안내

KeyScribe는 사용자가 녹음한 음성을 설정에서 선택한 OpenAI, ElevenLabs 또는 Groq의 음성 인식 API로 전송합니다. 모델 목록을 가져올 때도 선택한 서비스의 API에 요청합니다. 각 서비스의 데이터 처리와 비용은 해당 서비스의 정책 및 사용자 계정 설정을 따릅니다.

API 키는 사용자 기기의 `user_config.json`에 저장합니다. 경로는 macOS의 `~/Library/Application Support/keyscribe/`, Windows의 `%APPDATA%\keyscribe\`입니다. 앱 설정은 같은 폴더의 `config.toml`에 저장합니다. 전사가 실패해도 녹음을 다시 쓸 수 있도록 녹음 원본(WAV)을 같은 폴더의 `logs/`에 진단 로그와 함께 저장합니다. 보존 기간(1시간·1일·7일·30일, 기본 7일)은 설정에서 고를 수 있고, 기간이 지난 로그와 녹음은 앱이 자동으로 삭제합니다. 앱을 종료한 뒤 이 폴더를 삭제하면 KeyScribe의 로컬 설정, API 키, 로그와 녹음 원본이 제거됩니다.

KeyScribe 자체 계정이나 자체 음성 인식 서버는 없습니다. 소스 코드에는 별도 사용 통계 전송 기능이 없습니다.

Ubuntu 네이티브 앱은 설정과 API 키를 `$XDG_CONFIG_HOME/keyscribe/settings.ini`
(기본 `~/.config/keyscribe/settings.ini`)에 함께 저장합니다. 파일 권한은 0600이며
API 키는 평문으로 저장됩니다. 녹음 원본은 `$XDG_DATA_HOME/keyscribe/logs/`
(기본 `~/.local/share/keyscribe/logs/`)에 0600 권한으로 저장하고, 설정한 보관 기간에
따라 삭제합니다. Ubuntu 버전은 같은 폴더의 `debug.log`에 상태·HTTP 코드 등 진단 정보를 저장하며,
API 키와 전사 내용은 기록하지 않습니다. 로그에도 같은 보관 기간을 적용합니다.
자동 붙여넣기는 시스템 키보드 권한을 요청하며 화면은 캡처하지 않습니다.
취소하면 진행 중인 API 요청을 중단하지만 이미 서비스로 전송된 음성은 회수할 수 없습니다.
