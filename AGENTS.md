# KeyScribe 작업 지침

- 이 소스 저장소 `zidell/keyscribe`는 비공개로 유지한다. 사용자의 명시적인 요청 없이 공개로 전환하지 않는다.
- Homebrew 배포는 진행하지 않는다.
- 추후 Homebrew 배포를 결정하면 현실적인 자체 배포 방식은 이 저장소를 공개해 tap으로 쓰거나, 소스 저장소는 비공개로 두고 별도 공개 tap을 만드는 것이다. 두 경우 모두 DMG의 공개 다운로드 URL이 필요하다. DMG를 정적 페이지에 올리는 것만으로 Homebrew에 등록되지는 않는다.
- 공식 `homebrew/cask` 등록은 자체 tap과 다른 절차다. 공개 DMG URL과 cask 정의를 제출해 심사를 받아야 하며, 신생 앱은 공개적으로 확인할 수 있는 사용 실적이 실질적인 진입 조건이다. 저장소를 공개하는 것만으로 공식 등록을 보장하지 않는다.
- 릴리스 태그(`vMAJOR.MINOR.PATCH`)에서 macOS Apple Silicon/Intel DMG와 Windows x64 MSIX 및 실행 파일을 자동 빌드한다.
- 배포용 macOS 앱은 Developer ID Application 인증서로 서명하고 Apple 공증을 거친다. 로컬 키체인에는 `Developer ID Application: heunghyun lee (AF68GKBM82)`가 확인되었다. 개인키, 암호, API 키는 문서나 저장소에 넣지 않는다.
- Windows 배포 파일은 신뢰할 수 있는 코드 서명 인증서로 서명한다. 자체 서명이나 임의 기본 인증서만으로 일반 사용자의 설치 경고를 없앨 수 있다고 안내하지 않는다.
- 정적 랜딩 페이지는 `keyscribe.gitools.net`에서 제공한다. 릴리스 시 `static` 브랜치에 페이지를 게시한다. `static` 브랜치에는 소스 코드를 포함하지 않는다.
- Cloudflare Pages는 파일당 25 MiB 제한이 있으므로 DMG·EXE·MSIX를 Pages나 Git 브랜치에 넣지 않는다. 설치 파일은 Cloudflare R2에 저장하고 Pages Function이 같은 도메인의 `/downloads/` 경로에서 스트리밍한다. 추가 다운로드 도메인을 임의로 만들지 않는다.
- Cloudflare Pages는 릴리스 워크플로에서 `static` 브랜치의 페이지를 직접 배포한다. 도메인/DNS와 R2 버킷 연결은 외부 서비스 설정이 필요하다.
- macOS와 Windows 개발 중 소스 변경을 감지해 앱을 다시 빌드하고 재실행한다. macOS에서는 로그인 에이전트가 앱을 직접 실행하고 별도 `launchd` 작업이 변경된 소스만 빌드한다.
- `README.md`는 실행·사용법 요약만 담고, 릴리스·서명·페이지 설정은 `docs/release.md`에 기록한다.
- 이전 py2app macOS 앱의 대기 중 RSS는 이 Mac에서 약 76~91 MiB로 측정되었다. Python 없는 macOS Swift/AppKit 앱과 Windows Rust 앱이 `main`에 병합되었다. 20 MiB 안팎은 목표이며 실제 상주 메모리는 권한과 기능 검증을 마친 뒤 측정한다.
- `vMAJOR.MINOR.PATCH` 태그의 통합 릴리스 워크플로는 macOS와 Windows 네이티브 앱을 빌드한다. 네이티브 진행 상태와 남은 검증 항목은 `docs/native-rewrite.md`에 기록한다.
- `main`에 푸시할 때마다 Apple Silicon·Intel용 서명·공증 DMG를 비공개 GitHub 사전 릴리스로 게시한다. 고유 `macos-main-*` 태그를 사용하며 R2와 랜딩 페이지는 갱신하지 않는다.
- macOS 네이티브 앱은 `macos-vMAJOR.MINOR.PATCH` 태그로 별도 서명·공증 DMG 사전 릴리스도 만들 수 있다. 이 사전 릴리스는 랜딩 페이지를 갱신하지 않는다.
- `main`에는 네이티브 앱 소스만 유지한다. 이전 Python 앱 소스와 Python 기반 패키징 도구는 `python` 브랜치에 남겨 둔다.
- 이 Mac에서는 로그인 에이전트가 빌드된 네이티브 앱을 직접 실행한다. 별도 소스 감시 작업은 변경 시에만 `scripts/rebuild_macos_app.sh`를 실행한다. 앱은 셸 감시기의 자식 프로세스로 실행하지 않는다.
