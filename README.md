# KeyScribe

KeyScribe는 macOS 메뉴바와 Windows 트레이에서 실행되는 음성 받아쓰기 앱입니다. 단축키를 누르고 말하면 음성을 텍스트로 변환해 현재 입력창에 붙여넣습니다. 기본값은 키를 누르는 동안 녹음하는 방식이며, 설정에서 다시 누를 때 종료하는 방식으로 바꿀 수 있습니다.

## 설치 및 실행

[다운로드 페이지](https://keyscribe.gitools.net)에서 운영체제에 맞는 파일을 받습니다.

| 운영체제 | 파일 | 실행 |
| --- | --- | --- |
| macOS Apple Silicon | `KeyScribe-macos-arm64-*.dmg` | DMG를 열고 앱을 Applications로 복사 |
| macOS Intel | `KeyScribe-macos-x64-*.dmg` | DMG를 열고 앱을 Applications로 복사 |
| Windows 10/11 x64 | `KeyScribe-windows-x64-*.msix` | MSIX를 열어 설치한 뒤 시작 메뉴에서 실행 |

Windows에서는 설치 없이 실행하는 서명된 `.exe`도 다운로드할 수 있습니다.

소스에서 직접 실행하려면 Python 3.11 이상을 설치하고 다음 명령을 실행합니다.

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python main.py
```

Windows PowerShell에서는 첫 줄부터 `python -m venv .venv`로 실행하고, 활성화 명령을 `.venv\Scripts\Activate.ps1`로 바꿉니다.

소스를 수정할 때마다 앱을 다시 빌드해 실행하려면 macOS에서 `python -m pip install py2app`, Windows에서 `python -m pip install pyinstaller`를 추가로 실행한 뒤 `python scripts/dev.py`를 실행합니다. 빌드가 끝나면 감시 도구가 시작한 앱을 자동으로 재시작합니다.

## 처음 사용할 때

1. 메뉴바 또는 트레이의 KeyScribe 아이콘을 열고 **설정...**에서 ElevenLabs 또는 OpenAI API 키와 모델을 선택합니다.
2. macOS에서는 마이크와 **시스템 설정 → 개인 정보 보호 및 보안 → 손쉬운 사용** 권한을 허용합니다. Windows에서는 **설정 → 개인 정보 및 보안 → 마이크**에서 데스크톱 앱의 마이크 접근을 허용합니다.
3. 텍스트를 입력할 창에 커서를 놓고 오른쪽 Option(macOS) 또는 오른쪽 Alt(Windows)를 누른 채 말합니다. 키를 놓으면 전사 결과가 붙여넣어집니다.

설정에서 단축키, 녹음 종료 방식, 인식 언어, 자동 전송 여부를 바꿀 수 있습니다. 자동 전송을 켜면 붙여넣은 뒤 Enter도 누릅니다. 관리자 권한으로 실행한 Windows 앱에 자동 입력하려면 KeyScribe도 같은 권한으로 실행해야 할 수 있습니다.

## 문제 확인

로그는 macOS의 `~/Library/Logs/keyscribe/keyscribe.log`, Windows의 `%APPDATA%\keyscribe\Logs\keyscribe.log`에 저장됩니다.

릴리스 빌드, 서명, 다운로드 페이지 설정은 [배포 문서](docs/release.md)를 참고하세요.
