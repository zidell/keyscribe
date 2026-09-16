# KeyScribe 작업 지침

- 이 소스 저장소 `zidell/keyscribe`는 비공개로 유지한다. 사용자의 명시적인 요청 없이 공개로 전환하지 않는다.
- Homebrew 배포는 진행하지 않는다.
- 릴리스 태그(`vMAJOR.MINOR.PATCH`)에서 macOS Apple Silicon/Intel DMG와 Windows x64 MSIX 및 실행 파일을 자동 빌드한다.
- 배포용 macOS 앱은 Developer ID Application 인증서로 서명하고 Apple 공증을 거친다. 로컬 키체인에는 `Developer ID Application: heunghyun lee (AF68GKBM82)`가 확인되었다. 개인키, 암호, API 키는 문서나 저장소에 넣지 않는다.
- Windows 배포 파일은 신뢰할 수 있는 코드 서명 인증서로 서명한다. 자체 서명이나 임의 기본 인증서만으로 일반 사용자의 설치 경고를 없앨 수 있다고 안내하지 않는다.
- 정적 랜딩 페이지는 `keyscribe.gitools.net`에서 제공한다. 릴리스 시 `static` 브랜치에 페이지를 게시한다. `static` 브랜치에는 소스 코드를 포함하지 않는다.
- Cloudflare Pages는 파일당 25 MiB 제한이 있으므로 DMG·EXE·MSIX를 Pages나 Git 브랜치에 넣지 않는다. 설치 파일은 Cloudflare R2에 저장하고 Pages Function이 같은 도메인의 `/downloads/` 경로에서 스트리밍한다. 추가 다운로드 도메인을 임의로 만들지 않는다.
- Cloudflare Pages는 릴리스 워크플로에서 `static` 브랜치의 페이지를 직접 배포한다. 도메인/DNS와 R2 버킷 연결은 외부 서비스 설정이 필요하다.
- 개발 중에는 소스 변경을 감지해 앱을 다시 빌드하고 재실행하는 개발 모드를 제공한다. 감시 도구가 시작한 앱만 종료·재시작한다.
- `README.md`는 실행·사용법 요약만 담고, 릴리스·서명·페이지 설정은 `docs/release.md`에 기록한다.
- 현재 py2app macOS 앱의 대기 중 RSS는 이 Mac에서 약 76~91 MiB로 측정되었다. 20 MiB 안팎의 목표와 Python 제거는 논의 중이며, 재작성은 아직 결정되지 않았다.
