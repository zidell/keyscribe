# 배포 설정과 릴리스

유지 관리자를 위한 문서입니다. 앱 사용 방법은 [README](../README.md)에 있습니다. 소스 저장소 `zidell/keyscribe`는 MIT 라이선스로 공개합니다.

## 배포 구조

### `main` 푸시 미리보기

`main`에 커밋을 푸시할 때마다 [Main previews 워크플로](../.github/workflows/main-macos-release.yml)가 해당 커밋을 빌드합니다. Apple Silicon·Intel 앱과 DMG를 Developer ID로 서명하고 공증한 뒤, 두 DMG가 모두 검증되면 [GitHub Releases](https://github.com/zidell/keyscribe/releases)에 사전 릴리스로 게시합니다. 태그는 `macos-main-실행번호-커밋해시12자리` 형식이며, DMG 파일명에도 같은 식별자를 사용합니다. 앱 내부 미리보기 버전은 `0.1.실행번호` 형식입니다.

같은 워크플로는 Windows x64 Rust 앱도 매번 빌드합니다. Partner Center의 제품 Identity Secret 세 개가 모두 설정되면 `1.0.실행번호.0` 버전의 **Store 제출용 서명 없는 MSIX**를 만들고 manifest, 실행 파일, 서명 부재를 확인해 Actions artifact에만 올립니다. 세 값이 아직 없으면 Rust 빌드만 검증합니다. 일부만 있으면 설정 오류로 실패합니다. 이 MSIX는 Store 인증·재서명 전에는 직접 설치용으로 배포하지 않습니다. Windows 작업의 결과는 macOS DMG 게시와 독립적입니다.

이 미리보기는 R2, `static` 브랜치, 랜딩 페이지를 갱신하지 않습니다. 아래 `vMAJOR.MINOR.PATCH` 태그 경로는 별도의 Windows 코드 서명 인증서가 필요한 직접 다운로드 배포입니다.

### Windows Store 등록과 자동 업데이트

1. Partner Center에서 KeyScribe 앱 이름을 예약하고 **Product management → Product identity**의 `Package/Identity/Name`, `Package/Identity/Publisher`, `Package/Properties/PublisherDisplayName`을 복사합니다. 값을 추정하거나 수정하지 않습니다. [제품 Identity 안내](https://learn.microsoft.com/en-us/windows/apps/publish/view-app-identity-details)
2. Repository **Settings → Secrets and variables → Actions → Secrets**에 아래 세 값을 등록합니다. 다음 `main` 푸시부터 Actions의 `main-windows-store-x64` artifact에 Store 제출용 MSIX가 생성됩니다. 파일명에는 실행 번호와 커밋 해시가 들어갑니다. `1.0.실행번호.0`은 이전에 제출한 Store 버전보다 커야 하며 실행 번호가 65535에 도달하면 버전 정책을 갱신해야 합니다. [MSIX 버전 요구사항](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements)
3. 첫 제출은 Partner Center에서 직접 진행합니다. 가격·제공 지역, 설명·스크린샷, 개인정보 처리방침, 연령 등급, `runFullTrust` 기능 선언 등 필수 항목을 채우고 MSIX를 업로드해 인증을 마칩니다. Store는 승인된 MSIX를 재서명합니다. [첫 제출 안내](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/create-app-submission), [Store 서명 안내](https://learn.microsoft.com/en-us/windows/apps/publish/faq/get-started-with-the-microsoft-store)
4. 첫 제출 완료 후 Microsoft Store Submission API와 Entra 앱 자격 증명을 연결하면 새 MSIX 업로드·업데이트 제출을 CI에서 자동화할 수 있습니다. 현재 워크플로는 **빌드와 artifact 생성까지만** 자동화합니다. API 연결 전에는 새 버전을 Partner Center에 수동 제출해야 합니다. API로 제출한 버전은 심사를 거치고 승인되면 Store가 설치된 앱에 업데이트를 전달합니다. [Submission API](https://learn.microsoft.com/en-us/windows/uwp/monetize/manage-app-submissions), [Store 업데이트 안내](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/publish-first-app)

자동 제출 자격 증명은 **Partner Center → 계정 설정 → Users**에서 제출용 Microsoft Entra 앱을 추가하고 `Manager` 역할을 부여해 준비합니다. 해당 앱 화면의 Tenant ID와 Client ID를 기록한 뒤 **Add new key**에서 키를 발급합니다. Azure 화면으로 연결되면 **Entra ID → App registrations → 해당 앱 → Certificates & secrets → Client secrets → New client secret**에서 생성합니다. Secret의 **Value**는 한 번만 표시되므로 즉시 GitHub Actions Secret `KEYSCRIBE_STORE_CLIENT_SECRET`에 저장합니다. Tenant ID와 Client ID는 각각 `KEYSCRIBE_STORE_TENANT_ID`, `KEYSCRIBE_STORE_CLIENT_ID`로 저장할 예정입니다. 첫 제출 완료 전에는 이 Secret들이 있어도 자동 제출 API를 사용할 수 없습니다. [Microsoft의 앱 연결 및 키 발급 안내](https://learn.microsoft.com/en-us/windows/uwp/monetize/create-and-manage-submissions-using-windows-store-services)

| Repository Secret | Partner Center 제품 Identity 값 |
| --- | --- |
| `KEYSCRIBE_STORE_PACKAGE_NAME` | `Package/Identity/Name` |
| `KEYSCRIBE_STORE_PUBLISHER` | `Package/Identity/Publisher` |
| `KEYSCRIBE_STORE_PUBLISHER_DISPLAY_NAME` | `Package/Properties/PublisherDisplayName` |

### 네이티브 macOS 시험 릴리스

`main`의 macOS 앱은 [Native macOS release 워크플로](../.github/workflows/native-macos-release.yml)에서 별도로 배포할 수도 있습니다. `macos-vMAJOR.MINOR.PATCH` 태그를 `main`의 커밋에 붙여 푸시하면 Apple Silicon·Intel 앱을 Swift로 빌드하고 Developer ID로 서명한 뒤 DMG를 공증·스테이플합니다. 두 DMG가 모두 통과하면 **GitHub 사전 릴리스**에 올립니다. 각 DMG에는 앱과 Applications 바로가기가 있습니다.

```bash
git switch main
git tag macos-v0.1.2
git push origin macos-v0.1.2
```

같은 태그의 실행이 실패했다면 GitHub **Actions → Native macOS release → Run workflow**에서 기존 태그를 입력해 다시 실행할 수 있습니다. GitHub Secret은 아래 표의 macOS 관련 여섯 개가 필요합니다. Windows 인증서나 Cloudflare 설정은 이 흐름에 필요하지 않습니다. 이미 생성된 릴리스의 파일을 자동으로 덮어쓰지 않으므로, 게시 뒤 변경하려면 새 버전 태그를 사용합니다. 이 사전 릴리스는 랜딩 페이지를 갱신하지 않습니다.

### 통합 네이티브 릴리스

`vMAJOR.MINOR.PATCH` 태그를 올리면 [Release 워크플로](../.github/workflows/release.yml)가 다음 순서로 동작합니다.

1. macOS Apple Silicon·Intel 앱을 빌드하고 Developer ID로 서명·공증한 DMG 두 개를 만듭니다.
2. Windows x64 앱을 빌드하고 서명한 MSIX 설치 파일과 단독 EXE를 만듭니다.
3. 네 파일을 GitHub Release에 보관하고, 공개 다운로드용 파일은 Cloudflare R2에 업로드합니다.
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

Windows PFX Secret 두 개는 Store 제출용 MSIX 빌드에 사용하지 않습니다. 이 값이 없는 동안 아래 `v*` 직접 다운로드 통합 릴리스는 Windows 단계에서 실패하며, 서명 없는 Store 제출 파일로 대신 게시하지 않습니다.

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

## 개발 중 실행과 빌드

README의 네이티브 빌드 환경을 설치한 뒤 macOS에서는 `bash scripts/install_macos_login_app.sh`로 빌드된 앱의 로그인 실행과 소스 변경 자동 빌드를 등록합니다. 앱은 직접 실행되며, 별도의 `launchd` 작업이 소스 변경 시에만 빌드하고 재시작합니다. `bash scripts/rebuild_macos_app.sh`로 수동 재빌드할 수도 있습니다. Windows에서는 `native/windows/dev.ps1`이 소스와 에셋을 감시하며 변경 시 앱을 다시 빌드해 재시작합니다. 개발 빌드와 배포용 서명·공증은 별개입니다.
