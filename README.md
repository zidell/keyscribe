# KeyScribe

KeyScribe는 마이크로 녹음된 음성을 텍스트로 변환해 현재 입력창에 붙여넣는 보이스 입력 앱입니다. 가장 단순한 형태로 최소한의 기능만 제공하며, 네이티브로 구현해 대기 메모리 20MB 안팎을 목표로 합니다. 자체 모델은 포함하지 않으므로 OpenAI 또는 ElevenLabs API 키가 필요합니다.

## 특징

- 누르고 말하기와 토글 녹음, Esc 취소, 녹음 시간 제한을 지원합니다.
- 녹음과 변환 상태, 실시간 입력 음량을 화면 중앙 하단에 표시합니다.
- 설정에서 단축키, 언어, 모델, 효과음 음량, 자동 Enter 입력을 바꿀 수 있습니다.
- 메뉴의 **로그 보기**에서 최근 24시간의 진단 로그를 열 수 있습니다. API 키와 녹음·변환 내용은 로그에 남기지 않습니다.

## 만든 이유

시중에는 이미 많은 보이스 입력 앱이 있습니다. 유료·무료 앱을 여럿 써 보고 Reddit의 사용자 경험도 참고했습니다. 그 과정에서 로컬 STT 모델은 아직 정확도가 아쉽다고 느꼈고, 실사용에서는 OpenAI와 ElevenLabs의 STT API가 가장 정확하고 안정적인 선택지라고 판단했습니다. 유료들은 기능에 비해 가격이 높거나, 수많은 기능으로 버그 패치가 너무 자주 있었고, 대기하는 시간에도 메모리가 100MB 이상을 소비하는 증상이 심했습니다. 일부 무료 앱은 완성도나 배포 품질이 기대에 미치지 못했습니다.

## 화면 미리보기

아래 이미지는 앱 사용 흐름을 보여 주는 소개용 UI 이미지입니다.

| macOS | Windows |
| --- | --- |
| ![KeyScribe가 녹음, 변환, 자동 붙여넣기를 수행하는 macOS 흐름](assets/screenshots/keyscribe-flow.gif) | ![KeyScribe가 녹음, 변환, 자동 붙여넣기를 수행하는 Windows 흐름](assets/screenshots/keyscribe-windows-flow.gif) |

## 설치

[다운로드 페이지](https://keyscribe.gitools.net)에서 macOS 설치 파일을 받습니다. 최신 `main`의 macOS 설치용 DMG는 [GitHub Releases](https://github.com/zidell/keyscribe/releases)의 사전 릴리스에서도 받을 수 있습니다. Windows 앱은 GitHub Releases에서 SignPath로 서명된 파일을 받습니다.

| 운영체제 | 파일 | 설치 또는 실행 |
| --- | --- | --- |
| macOS Apple Silicon | `KeyScribe-macos-arm64-*.dmg` | DMG를 열고 앱을 Applications로 복사 |
| macOS Intel | `KeyScribe-macos-x64-*.dmg` | DMG를 열고 앱을 Applications로 복사 |
| Windows 10/11 x64 | `KeyScribe-windows-x64-*.msix` | GitHub Releases에서 SignPath 서명 설치 파일 다운로드 |

## 처음 사용할 때

1. 메뉴 막대 또는 트레이의 KeyScribe 아이콘에서 **설정...**을 열고 ElevenLabs 또는 OpenAI API 키와 모델을 선택합니다.
2. macOS에서는 마이크와 **시스템 설정 → 개인 정보 보호 및 보안 → 손쉬운 사용** 권한을 허용합니다. Windows에서는 **설정 → 개인 정보 및 보안 → 마이크**에서 데스크톱 앱의 마이크 접근을 허용합니다.
3. 텍스트를 입력할 창에 커서를 놓고 오른쪽 Command(macOS) 또는 오른쪽 Alt(Windows)를 누른 채 말합니다. 키를 놓으면 전사 결과가 붙여넣어집니다.

설정에서 단축키, 녹음 종료 방식, 녹음 시간 제한(10·20·30·60분, 기본 30분), 인식 언어, 녹음 시작 효과음 음량, 자동 전송 여부를 바꿀 수 있습니다. 자동 전송을 켜면 붙여넣은 뒤 Enter도 누릅니다. 관리자 권한으로 실행한 Windows 앱에 자동 입력하려면 KeyScribe도 같은 권한으로 실행해야 할 수 있습니다.

## 문제 확인

앱의 상태와 오류는 메뉴 막대 또는 트레이 메뉴에서 확인할 수 있습니다. 메뉴의 **로그 보기**는 앱 진단 로그를 엽니다.

- macOS: `~/Library/Application Support/keyscribe/debug.log`
- Windows: `%APPDATA%\keyscribe\debug.log`
- 개발 실행: `dist-native/macos-debug.log`, `dist-native/windows-debug.log`

macOS 로그인 앱과 소스 감시 빌드 로그는 `dist-native/app.log`, `dist-native/watch.log`에 저장됩니다.

음성 전송과 API 키 저장 방식은 [개인정보 안내](docs/privacy.md)에 설명되어 있습니다.

소스 코드는 [MIT 라이선스](LICENSE)로 공개합니다.
