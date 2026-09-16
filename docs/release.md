# 배포 설정과 릴리스

유지 관리자를 위한 문서입니다. 앱 사용 방법은 [README](../README.md)에 있습니다. 소스 저장소 `zidell/keyscribe`는 **비공개**로 유지합니다.

## 배포 구조

`vMAJOR.MINOR.PATCH` 태그를 올리면 [Release 워크플로](../.github/workflows/release.yml)가 다음 순서로 동작합니다.

1. macOS Apple Silicon·Intel 앱을 빌드하고 Developer ID로 서명·공증한 DMG 두 개를 만듭니다.
2. Windows x64 앱을 빌드하고 서명한 MSIX 설치 파일과 단독 EXE를 만듭니다.
3. 네 파일을 비공개 GitHub Release에 보관하고, 공개 다운로드용 파일은 Cloudflare R2에 업로드합니다.
4. [랜딩 페이지](../site/index.html)에 해당 버전의 다운로드 링크를 채워 `static` 브랜치에 게시하고 Cloudflare Pages에 직접 배포합니다. [Pages Worker](../site/_worker.js)가 같은 도메인의 `/downloads/` 요청을 R2에서 스트리밍합니다.

`static` 브랜치에는 HTML과 버전 표기만 들어갑니다. 매 릴리스마다 생성된 브랜치로 갱신하며 이전 브랜치 기록은 유지하지 않습니다. 앱 바이너리는 Cloudflare Pages의 [파일당 25 MiB 제한](https://developers.cloudflare.com/pages/platform/limits/)과 Git의 [100 MiB 파일 제한](https://docs.github.com/en/repositories/creating-and-managing-repositories/repository-limits/) 때문에 브랜치에 넣지 않습니다.

파일 이름은 아래와 같습니다.

| 파일 | 용도 |
| --- | --- |
| `KeyScribe-macos-arm64-VERSION.dmg` | Apple Silicon Mac |
| `KeyScribe-macos-x64-VERSION.dmg` | Intel Mac |
| `KeyScribe-windows-x64-VERSION.exe` | Windows에서 설치 없이 실행 |
| `KeyScribe-windows-x64-VERSION.msix` | Windows 설치 파일 |

개인 `config.toml`과 API 키는 번들에 포함되지 않습니다. 설정은 앱 실행 후 사용자별 디렉터리에 저장됩니다.

## GitHub Actions 비밀 값

Repository **Settings → Secrets and variables → Actions**에 다음 값을 등록합니다. 인증서와 개인키 파일은 Git에 추가하지 않습니다.

| Secret | 값 |
| --- | --- |
| `KEYSCRIBE_MAC_CERTIFICATE_BASE64` | Developer ID Application 인증서와 개인키를 내보낸 `.p12`의 Base64 |
| `KEYSCRIBE_MAC_CERTIFICATE_PASSWORD` | `.p12` 암호 |
| `KEYSCRIBE_MAC_SIGNING_IDENTITY` | `Developer ID Application: heunghyun lee (AF68GKBM82)` |
| `KEYSCRIBE_APPLE_ID` | 공증에 사용할 Apple ID |
| `KEYSCRIBE_APPLE_APP_PASSWORD` | 해당 Apple ID의 앱 전용 암호 |
| `KEYSCRIBE_APPLE_TEAM_ID` | `AF68GKBM82` |
| `KEYSCRIBE_WINDOWS_PFX_BASE64` | 신뢰 가능한 Windows 코드 서명 인증서와 개인키를 내보낸 `.pfx`의 Base64 |
| `KEYSCRIBE_WINDOWS_PFX_PASSWORD` | `.pfx` 암호 |
| `KEYSCRIBE_CLOUDFLARE_ACCOUNT_ID` | Cloudflare 계정 ID |
| `KEYSCRIBE_CLOUDFLARE_API_TOKEN` | Pages 프로젝트 배포 권한이 있는 API 토큰 |
| `KEYSCRIBE_R2_ACCESS_KEY_ID` | R2 객체 쓰기 권한이 있는 S3 API 키 ID |
| `KEYSCRIBE_R2_SECRET_ACCESS_KEY` | 위 키의 비밀 값 |

`base64 < certificate.p12 | tr -d '\n'`(macOS) 또는 `[Convert]::ToBase64String([IO.File]::ReadAllBytes('certificate.pfx'))`(Windows PowerShell)로 인증서의 Base64 값을 만들 수 있습니다. 로컬 Mac 키체인에는 위 Developer ID 인증서가 확인되었지만 Actions에는 개인키를 포함한 `.p12`를 별도로 등록해야 합니다.

다음 Repository **Variables**는 선택 사항이며 생략 시 오른쪽 기본값을 사용합니다.

| Variable | 기본값 |
| --- | --- |
| `KEYSCRIBE_R2_BUCKET` | `keyscribe-downloads` |
| `KEYSCRIBE_PAGES_PROJECT` | `keyscribe` |

## Cloudflare 설정

1. R2에서 `keyscribe-downloads` 버킷을 만듭니다. 공개 버킷이나 추가 도메인은 필요하지 않습니다. R2 API 토큰은 해당 버킷의 객체 쓰기 범위로 제한합니다.
2. Cloudflare Pages에 `keyscribe` 프로젝트를 **Direct Upload** 방식으로 만들고 production branch를 `static`으로 설정합니다. Wrangler를 사용한다면 `npx wrangler pages project create keyscribe --production-branch static`으로 만들 수 있습니다. 워크플로가 페이지를 직접 배포하므로 Git 저장소 연결은 필요하지 않습니다. API 토큰에 Pages 배포 권한을 부여합니다. Pages 프로젝트의 **Settings → Bindings**에서 R2 버킷을 `KEYSCRIBE_RELEASES`라는 이름으로 연결하고, Production과 Preview 환경 모두에 같은 버킷을 지정합니다. [Pages R2 binding 안내](https://developers.cloudflare.com/pages/functions/bindings/)
3. Pages 프로젝트의 **Custom domains**에서 `keyscribe.gitools.net`을 추가합니다. DNS가 Cloudflare에서 관리되지 않는다면 안내된 `<project>.pages.dev` 대상으로 CNAME을 만듭니다. [Pages 도메인 안내](https://developers.cloudflare.com/pages/configuration/custom-domains/)
4. 첫 릴리스가 끝나면 페이지의 네 다운로드 링크가 모두 열리는지 확인합니다. R2 binding이 누락되면 `/downloads/`에서 503을 반환합니다.

Cloudflare Pages에 설치 파일을 직접 업로드하지 않습니다. R2의 `releases/VERSION/` 경로에 버전별 파일이 남고, 랜딩 페이지는 최신 버전으로 갱신됩니다. 다운로드 주소는 `https://keyscribe.gitools.net/downloads/파일명` 하나만 사용합니다. Pages Worker는 파일을 통째로 메모리에 올리지 않고 스트리밍합니다. [Cloudflare의 R2 binding](https://developers.cloudflare.com/pages/functions/bindings/)과 [응답 크기 제한](https://developers.cloudflare.com/workers/platform/limits/)을 참고하세요.

## 서명과 설치 경고

macOS 배포본은 Developer ID 인증서의 hardened runtime 서명과 Apple 공증을 거칩니다. [Apple 공증 안내](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)

Windows MSIX의 Publisher는 `.pfx` 인증서 주체에서 생성합니다. 업데이트를 위해 다음 버전에도 **같은 주체**를 유지해야 합니다. 일반 사용자에게 별도 인증서 설치를 요구하지 않으려면 신뢰 가능한 코드 서명 수단이 필요합니다. 자체 서명 인증서는 그 요구를 충족하지 않습니다. 서명된 EXE에도 SmartScreen 평판 경고가 발생할 수 있으므로 경고가 절대 나오지 않는다고 보장하지 않습니다. [Microsoft의 MSIX 서명 안내](https://learn.microsoft.com/en-us/windows/msix/package/sign-msix-package-guide)

## 릴리스 발행과 확인

`main`에 릴리스할 변경을 반영한 뒤 새 버전 태그를 올립니다.

```bash
git tag v1.0.0
git push origin v1.0.0
```

GitHub **Actions → Release**에서 모든 작업을 확인합니다. 실패한 버전 번호는 재사용하지 않고 원인을 수정한 뒤 새 태그를 사용합니다. 서명 자격 증명이나 Cloudflare 설정이 없으면 워크플로가 실패하며, 서명되지 않은 파일을 공개하지 않습니다.

완료 후 `https://keyscribe.gitools.net`에서 네 다운로드를 시험하고, 실제 macOS·Windows에서 앱을 실행해 마이크, 단축키, 붙여넣기 동작을 확인합니다. CI는 패키징·서명·업로드만 검증하며 GUI 동작은 자동 검증하지 않습니다.

## 개발 중 자동 빌드

README의 개발 환경을 설치한 뒤 macOS에서는 `py2app`, Windows에서는 `pyinstaller`를 추가로 설치하고 `python scripts/dev.py`를 실행합니다. 이 도구는 `main.py`, `i18n.py`, `setup.py`, 설정 템플릿과 에셋을 감시하며 변경 시 개발용 앱을 다시 빌드해 재시작합니다. 감시 도구가 시작한 프로세스만 종료합니다. 개발 빌드와 배포용 서명·공증은 별개입니다.
