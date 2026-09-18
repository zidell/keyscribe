# 개인정보 안내

KeyScribe는 사용자가 녹음한 음성을 설정에서 선택한 OpenAI, ElevenLabs 또는 Groq의 음성 인식 API로 전송합니다. 모델 목록을 가져올 때도 선택한 서비스의 API에 요청합니다. 각 서비스의 데이터 처리와 비용은 해당 서비스의 정책 및 사용자 계정 설정을 따릅니다.

API 키는 사용자 기기의 `user_config.json`에 저장합니다. 경로는 macOS의 `~/Library/Application Support/keyscribe/`, Windows의 `%APPDATA%\keyscribe\`입니다. 앱 설정은 같은 폴더의 `config.toml`에 저장합니다. 앱을 종료한 뒤 이 폴더를 삭제하면 KeyScribe의 로컬 설정과 API 키가 제거됩니다.

KeyScribe 자체 계정이나 자체 음성 인식 서버는 없습니다. 소스 코드에는 별도 사용 통계 전송 기능이 없습니다.
