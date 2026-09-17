# 네이티브 전환 작업

macOS와 Windows 네이티브 앱이 `main`에 병합되었다. `main` 푸시마다 macOS DMG 미리보기 사전 릴리스를 만들고, `vMAJOR.MINOR.PATCH` 태그는 두 플랫폼의 통합 네이티브 릴리스를 만든다. `macos-vMAJOR.MINOR.PATCH` 태그도 macOS 전용 사전 릴리스에 사용할 수 있다.

## 목표와 현재 상태

Python 런타임 번들 없이 macOS와 Windows에서 기존 받아쓰기 동작을 유지하고, 대기 중 메모리를 실제 기기에서 다시 측정한다. 20 MiB는 목표이며 보장값이 아니다.

macOS Swift/AppKit 실행 파일의 첫 릴리스 빌드는 `bash native/macos/build.sh`로 생성된다. 메뉴 막대, 사용자 설정 이관, 마이크 녹음, 화면 중앙 하단의 녹음·변환 상태 오버레이와 실시간 음량 막대, 단축키의 누르기/토글 방식, Esc 취소, OpenAI/ElevenLabs 전사, 클립보드 붙여넣기와 자동 Enter를 구현했다. 네트워크 취소와 늦게 도착한 응답을 구분한다.

설정창은 API 키의 제공자에 맞는 전사 모델 선택과 새로고침, 전사 언어·단축키 선택, 여러 줄 고유명사 입력을 제공한다. `군더더기 말 제거`는 ElevenLabs의 `scribe_v2` 또는 `scribe_v2_medical` 선택 시에만 활성화된다. 실제 계정에서 모델 목록을 불러오는 동작은 아직 수동 확인이 필요하다.

`KEYSCRIBE_CODESIGN_IDENTITY='Developer ID Application: heunghyun lee (AF68GKBM82)' bash native/macos/build.sh`로 로컬 앱을 같은 개발자 ID로 서명할 수 있다. `bash scripts/install_macos_login_app.sh`는 빌드된 앱을 로그인 시 직접 실행하고 소스 변경 시 자동으로 빌드·재실행하도록 등록한다. `bash scripts/rebuild_macos_app.sh`로 수동 재빌드할 수도 있다.

## 완료 전 검증 항목

- macOS 실제 앱에서 마이크와 손쉬운 사용 권한, 좌우 수정키, 녹음 중 장치 변경, 무음/짧은 녹음, API 응답, 붙여넣기를 확인한다.
- 기존 앱의 언어별 UI와 모든 단축키 선택지를 네이티브 앱에 이식한다.
- Windows 네이티브 앱과 MSIX/EXE 빌드 및 서명 경로를 실제 Windows에서 확인한다.
- 두 플랫폼의 대기 중 RSS를 동일한 조건에서 측정한다.

`macos-vMAJOR.MINOR.PATCH` 태그는 [네이티브 macOS 워크플로](../.github/workflows/native-macos-release.yml)를 실행해 공증된 Apple Silicon·Intel DMG를 GitHub 사전 릴리스에 올린다. `v*` 태그는 macOS와 Windows의 통합 네이티브 릴리스에 사용한다. 자세한 절차는 [배포 문서](release.md)에 있다.
