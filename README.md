# KeyScribe

KeyScribe는 macOS 메뉴바와 Windows 트레이에서 실행되는 음성 받아쓰기 앱입니다. 단축키를 누르고 말하면 음성을 텍스트로 변환해 현재 입력창에 붙여넣습니다. 기본값은 키를 누르는 동안 녹음하는 방식이며, 설정에서 다시 누를 때 종료하는 방식으로 바꿀 수 있습니다.

## 설치 및 실행

[다운로드 페이지](https://keyscribe.gitools.net)에서 운영체제에 맞는 파일을 받습니다. 현재 Windows Store 배포는 준비 중이며, 첫 등록이 승인되면 Microsoft Store에서 설치할 수 있습니다.

최신 `main`의 macOS 설치용 DMG는 [GitHub Releases](https://github.com/zidell/keyscribe/releases)의 사전 릴리스에서 받을 수 있습니다.

| 운영체제 | 파일 | 실행 |
| --- | --- | --- |
| macOS Apple Silicon | `KeyScribe-macos-arm64-*.dmg` | DMG를 열고 앱을 Applications로 복사 |
| macOS Intel | `KeyScribe-macos-x64-*.dmg` | DMG를 열고 앱을 Applications로 복사 |
| Windows 10/11 x64 | Microsoft Store | 첫 등록·심사 완료 후 Store에서 설치 |

macOS 네이티브 개발 버전은 Swift가 설치된 Mac에서 아래처럼 빌드하고 실행할 수 있습니다.

```bash
bash native/macos/build.sh
open dist-native/KeyScribe.app
```

Windows 네이티브 개발 버전은 Rust와 MSVC C++ 빌드 도구 또는 LLVM-MinGW가 설치된 PowerShell에서 빌드합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\native\windows\build.ps1
.\dist-native\KeyScribe.exe
```

macOS에서 빌드된 앱을 로그인할 때 자동 실행하고, 소스 변경 시에만 빌드·재실행하려면 첫 번째 명령을 한 번 실행합니다. 두 번째 명령은 수동으로 다시 빌드할 때 사용합니다.

```bash
bash scripts/install_macos_login_app.sh
bash scripts/rebuild_macos_app.sh
```

Windows에서 소스 변경을 감시하며 앱을 자동으로 다시 빌드하려면 다음 명령을 실행합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\native\windows\dev.ps1
```

## 처음 사용할 때

1. 메뉴바 또는 트레이의 KeyScribe 아이콘을 열고 **설정...**에서 ElevenLabs 또는 OpenAI API 키와 모델을 선택합니다.
2. macOS에서는 마이크와 **시스템 설정 → 개인 정보 보호 및 보안 → 손쉬운 사용** 권한을 허용합니다. Windows에서는 **설정 → 개인 정보 및 보안 → 마이크**에서 데스크톱 앱의 마이크 접근을 허용합니다.
3. 텍스트를 입력할 창에 커서를 놓고 오른쪽 Option(macOS) 또는 오른쪽 Alt(Windows)를 누른 채 말합니다. 키를 놓으면 전사 결과가 붙여넣어집니다.

설정에서 단축키, 녹음 종료 방식, 인식 언어, 녹음 시작 효과음 음량, 자동 전송 여부를 바꿀 수 있습니다. 효과음은 기본 100%이며 0%로 설정하면 꺼지고 최대 200%까지 조절할 수 있습니다. 자동 전송을 켜면 붙여넣은 뒤 Enter도 누릅니다. 관리자 권한으로 실행한 Windows 앱에 자동 입력하려면 KeyScribe도 같은 권한으로 실행해야 할 수 있습니다.

## 문제 확인

네이티브 앱의 현재 상태와 오류는 메뉴바 또는 트레이 메뉴에서 확인할 수 있습니다. macOS 로그인 앱과 자동 빌드 로그는 각각 `dist-native/app.log`, `dist-native/watch.log`에 저장됩니다.

릴리스 빌드, 서명, 다운로드 페이지 설정은 [배포 문서](docs/release.md)를 참고하세요.

소스 코드는 [MIT 라이선스](LICENSE)로 공개합니다. 음성 전송과 API 키 저장 방식은 [개인정보 안내](docs/privacy.md)에, 배포 파일의 서명 절차는 [Code signing policy](docs/code-signing-policy.md)에 설명되어 있습니다.
