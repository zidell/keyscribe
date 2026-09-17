# Code signing policy

KeyScribe의 공개 배포 파일은 이 저장소의 릴리스 태그에서 GitHub Actions로 빌드하고, 서명과 파일 내용을 확인한 뒤 게시합니다. macOS DMG는 Apple Developer ID로 서명하고 공증합니다. Windows 직접 다운로드용 EXE와 MSIX는 신뢰받는 서명이 준비되기 전까지 게시하지 않습니다.

Windows 배포에는 SignPath Foundation의 무료 오픈소스 서명을 신청할 계획입니다. 아직 승인되지 않았으며 SignPath Foundation이 현재 KeyScribe를 서명하거나 보증하는 것은 아닙니다. 승인 후 배포 페이지에 “Free code signing provided by SignPath.io, certificate by SignPath Foundation” 문구와 실제 서명 검증 절차를 반영합니다.

- 소스 작성자와 외부 변경 검토자: `zidell`
- 릴리스 서명 승인자: `zidell`
- 개인정보 안내: [privacy.md](privacy.md)

서명 요청과 릴리스 승인은 저장소 관리자가 수행합니다. GitHub Actions의 인증서 개인키와 서비스 자격 증명은 Secrets에 보관하며 소스 저장소에 넣지 않습니다.
