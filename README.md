# voice-stt

macOS 메뉴바에서 실행되는 음성 → 텍스트 변환 도구입니다.  
단축키를 누르고 있는 동안 녹음되고, 키를 놓으면 ElevenLabs Scribe v2 API로 텍스트 변환 후 현재 입력창에 자동으로 붙여넣기 + Enter 합니다.

---

## 구조

```
voice-stt/
├── main.py           # 메인 앱 (메뉴바 + 키 감지 + 녹음 + STT + 붙여넣기)
├── config.json       # 언어, keyterms 설정
├── setup.py          # py2app 빌드 설정
├── requirements.txt  # 의존 패키지 목록
└── README.md
```

API Key는 앱 메뉴의 **"API Key 설정..."** 에서 입력하면 각 컴퓨터의  
`~/Library/Application Support/voice-stt/user_config.json` 에 저장됩니다.

### 주요 흐름

```
오른쪽 Option 누름
    → 마이크 녹음 시작 (메뉴바 아이콘: 🔴)

오른쪽 Option 뗌
    → 녹음 종료 → WAV 임시 파일 저장 (메뉴바 아이콘: ⏳)
    → ElevenLabs Scribe v2 API 호출
    → 텍스트 수신 → 클립보드 복사 → Cmd+V → Enter (메뉴바 아이콘: 🎙)
```

---

## 요구사항

- macOS
- Python 3.10+
- ElevenLabs API 키 ([elevenlabs.io](https://elevenlabs.io) → Profile → API Keys)

---

## 설치 및 실행

```bash
cd ~/Sites/voice-stt

# 가상환경 (선택)
python3 -m venv .venv
source .venv/bin/activate

# 패키지 설치
pip3 install -r requirements.txt

# 실행
python3 main.py
```

실행하면 macOS 메뉴바에 🎙 아이콘이 생깁니다.

### API Key 입력

메뉴바 🎙 아이콘 클릭 → **"API Key 설정..."** → 키 입력 후 저장.  
설정은 `~/Library/Application Support/voice-stt/user_config.json` 에 저장되어 재시작 후에도 유지됩니다.

### 최초 실행 시 권한 허용 필요

| 권한 | 용도 |
|------|------|
| 마이크 | 음성 녹음 |
| 손쉬운 사용 (Accessibility) | 전역 키 감지 및 키 입력 시뮬레이션 |

> 손쉬운 사용 권한은 **시스템 설정 → 개인 정보 보호 및 보안 → 손쉬운 사용**에서 터미널(또는 앱)을 허용해야 합니다.

---

## 사용 방법

1. `python3 main.py` 실행
2. 텍스트를 입력할 창(채팅, 문서 등)에 포커스
3. **오른쪽 Option 키를 누른 채로** 말하기
4. 말이 끝나면 **키를 놓기**
5. 잠시 후 텍스트가 자동으로 입력되고 Enter까지 눌림

---

## 빌드 (.app 번들 생성)

소스 없이 실행 가능한 macOS 앱으로 패키징합니다.

```bash
# py2app 설치 (처음 한 번만)
pip3 install py2app

# 빌드
python3 setup.py py2app
```

완성된 앱: `dist/voice-stt.app`

### 다른 Mac에 배포

**같은 아키텍처끼리는 빌드 한 번으로 복사해서 사용 가능합니다.**

| 빌드 머신 → 실행 머신 | 가능 여부 |
|---|---|
| Intel Mac → Intel Mac | ✅ |
| Apple Silicon → Apple Silicon (M1/M2/M3) | ✅ |
| Intel ↔ Apple Silicon 교차 | ❌ 각 머신에서 따로 빌드 필요 |

배포 시 주의사항:

1. **API Key**: 앱 번들에 포함되지 않습니다. 다른 Mac에서 앱 실행 후 메뉴의 "API Key 설정..."에서 입력하세요.
2. **최초 실행**: Gatekeeper 경고 시 **우클릭 → 열기** 로 실행.
3. **권한**: 마이크 및 손쉬운 사용 권한을 그 Mac에서도 허용해야 합니다.

---

## config.json 설정

```json
{
  "shortcut": "right_option",
  "language": "ko",
  "keyterms": []
}
```

| 키 | 설명 |
|----|------|
| `shortcut` | 현재 `right_option` 고정 |
| `language` | 언어 코드 (`ko`, `en` 등) |
| `keyterms` | 인식을 강화할 단어 목록 (예: `["ChatGPT", "클로드"]`) |
