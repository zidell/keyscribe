# KeyScribe

**단축키를 누르고 말하면, 전사 결과가 현재 앱에 입력됩니다.**<br>
KeyScribe는 macOS 메뉴바와 Windows 트레이에서 실행되는 push-to-talk 음성 받아쓰기 도구입니다. 단축키를 누르는 동안 녹음하고, 키를 놓으면 음성을 텍스트로 변환해 현재 입력창에 자동으로 붙여넣습니다.

---

## 구조

```
keyscribe/
├── main.py           # 메인 앱 (메뉴바 + 키 감지 + 녹음 + STT + 붙여넣기)
├── config.toml       # 언어, 모델, keyterms 설정 (첫 실행 시 생성)
├── setup.py          # py2app 빌드 설정
├── requirements.txt  # 의존 패키지 목록
└── README.md
```

API Key는 앱 메뉴의 **"설정..."** 에서 입력하면 각 컴퓨터의<br>
`~/Library/Application Support/keyscribe/user_config.json` 에 저장됩니다.

### 주요 흐름

```
오른쪽 Option 누름
    → 마이크 녹음 시작 (메뉴바 아이콘: 🔴)

오른쪽 Option 뗌
    → 녹음 종료 → FLAC 임시 파일 저장 (메뉴바 아이콘: ⏳)
    → ElevenLabs Scribe 또는 OpenAI 전사 API 호출
    → 텍스트 수신 → 클립보드 복사 → Cmd+V → (자동 전송 선택 시 Enter) (메뉴바 아이콘: 🎙)
```

---

## 요구사항

- macOS 또는 Windows 10/11
- Python 3.11+
- ElevenLabs 또는 OpenAI API 키

---

## 설치 및 실행

### macOS

```bash
cd ~/Sites/keyscribe

# 가상환경 (선택)
python3 -m venv .venv
source .venv/bin/activate

# 패키지 설치
pip3 install -r requirements.txt

# 실행
python3 main.py
```

실행하면 macOS 메뉴바에 🎙 아이콘이 생깁니다.

### Windows

```powershell
cd C:\Users\<사용자>\keyscribe

# 가상환경 (선택)
python -m venv .venv
.venv\Scripts\activate

# 패키지 설치
pip install -r requirements.txt

# 실행
python main.py
```

실행하면 Windows 시스템 트레이에 원형 아이콘이 생깁니다.

### API Key 입력 및 모델 선택

메뉴바 🎙 아이콘 클릭 → **"설정..."** → API Key를 입력하고 모델을 선택한 뒤 저장합니다.<br>
설정은 `~/Library/Application Support/keyscribe/user_config.json` 에 저장되어 재시작 후에도 유지됩니다.

앱 화면 언어는 **음성 인식 언어**를 기준으로 자동 적용됩니다. 한국어(`ko`), 영어(`en`), 일본어(`ja`), 중국어(`zh`), 스페인어(`es` 및 `es-MX` 등)는 해당 UI 언어로 표시되며, 그 밖의 인식 언어는 영어 UI로 표시됩니다.

### 최초 실행 시 권한 허용 필요

**macOS**

| 권한 | 용도 |
|------|------|
| 마이크 | 음성 녹음 |
| 손쉬운 사용 (Accessibility) | 전역 키 감지 및 키 입력 시뮬레이션 |

> 손쉬운 사용 권한은 **시스템 설정 → 개인 정보 보호 및 보안 → 손쉬운 사용**에서 터미널(또는 앱)을 허용해야 합니다.

**Windows**

별도 권한 설정 불필요. 단, 키 입력 시뮬레이션이 관리자 권한으로 실행 중인 앱(일부 게임 등)에서 동작하지 않을 수 있습니다. 그 경우 `python main.py`를 관리자 권한으로 실행하세요.

---

## 사용 방법

1. `python3 main.py` 실행
2. 텍스트를 입력할 창(채팅, 문서 등)에 포커스
3. **오른쪽 Option 키를 누른 채로** 말하기
4. 말이 끝나면 **키를 놓기**
5. 잠시 후 텍스트가 자동으로 입력됨. 설정에서 **붙여넣기 후 자동 전송**을 켜면 Enter도 함께 눌림

---

## 터미널 없이 백그라운드 실행 (launchd)

빌드 없이 `python3 main.py`를 직접 실행합니다. macOS launchd에 등록하면 로그인 시 자동 시작되고, 터미널을 닫아도 계속 실행됩니다.

### 등록

```bash
# 로그 디렉토리 생성
mkdir -p ~/Library/Logs/keyscribe

# launchd에 등록 및 즉시 시작
launchctl load ~/Library/LaunchAgents/com.videostew.keyscribe.plist
```

> plist 파일: `~/Library/LaunchAgents/com.videostew.keyscribe.plist`

### 자주 쓰는 명령어

```bash
# 상태 확인
launchctl list | grep keyscribe

# 재시작 (main.py 수정 후)
launchctl unload ~/Library/LaunchAgents/com.videostew.keyscribe.plist && launchctl load ~/Library/LaunchAgents/com.videostew.keyscribe.plist

# 중지
launchctl unload ~/Library/LaunchAgents/com.videostew.keyscribe.plist
```

### 로그

| 파일 | 내용 |
|------|------|
| `~/Library/Logs/keyscribe/keyscribe.log` | 앱 로그 (녹음/STT/오류 등) |
| `~/Library/Logs/keyscribe/launchd-stderr.log` | launchd stderr (import 오류 등) |

---

## 빌드 (.app 번들 생성)

소스 없이 실행 가능한 macOS 앱으로 패키징합니다.

```bash
# py2app 설치 (처음 한 번만)
pip3 install py2app

# 빌드
python3 setup.py py2app
```

완성된 앱: `dist/KeyScribe.app`
빌드 머신에 Developer ID Application 인증서가 하나 있으면 자동으로 해당 인증서로 서명합니다.
인증서가 여러 개라면 `KEYSCRIBE_CODESIGN_IDENTITY`에 사용할 인증서 이름을 지정하세요.
임시 서명(`-`)으로 빌드하면 빌드마다 macOS 접근성 권한을 다시 허용해야 할 수 있습니다.

### 다른 Mac에 배포

**같은 아키텍처끼리는 빌드 한 번으로 복사해서 사용 가능합니다.**

| 빌드 머신 → 실행 머신 | 가능 여부 |
|---|---|
| Intel Mac → Intel Mac | ✅ |
| Apple Silicon → Apple Silicon (M1/M2/M3) | ✅ |
| Intel ↔ Apple Silicon 교차 | ❌ 각 머신에서 따로 빌드 필요 |

배포 시 주의사항:

1. **API Key**: 앱 번들에 포함되지 않습니다. 다른 Mac에서 앱 실행 후 메뉴의 "설정..."에서 입력하세요.
2. **최초 실행**: 정식 배포본은 Developer ID 서명·Apple 공증 후 배포해야 Gatekeeper 경고 없이 열립니다.
3. **권한**: 마이크 및 손쉬운 사용 권한을 그 Mac에서도 허용해야 합니다.

---

## config.toml 설정

```toml
shortcut = "right_option"
auto_send = true
language = "ko"
keyterms = []
```

| 키 | 설명 |
|----|------|
| `shortcut` | 설정 화면에서 OS별 지원 키를 선택 |
| `auto_send` | `true`면 붙여넣기 후 Enter를 눌러 자동 전송 |
| `language` | 언어 코드 (`ko`, `en` 등) |
| `keyterms` | 인식을 강화할 단어 목록 (예: `["ChatGPT", "클로드"]`) |
