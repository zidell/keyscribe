use serde::{Deserialize, Deserializer, Serialize};
use std::{env, fs, io, path::PathBuf};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Provider {
    OpenAi,
    ElevenLabs,
    Groq,
}

impl Provider {
    pub fn from_api_key(key: &str) -> Option<Self> {
        if key.starts_with("sk-") {
            Some(Self::OpenAi)
        } else if key.starts_with("sk_") {
            Some(Self::ElevenLabs)
        } else if key.starts_with("gsk_") {
            Some(Self::Groq)
        } else {
            None
        }
    }

    pub fn openai_compatible(self) -> bool {
        matches!(self, Self::OpenAi | Self::Groq)
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub shortcut: String,
    pub recording_control: String,
    #[serde(
        default = "default_recording_limit",
        deserialize_with = "deserialize_recording_limit"
    )]
    pub recording_time_limit_minutes: u16,
    #[serde(
        default = "default_log_retention",
        deserialize_with = "deserialize_log_retention"
    )]
    pub log_retention_hours: u32,
    pub auto_send: bool,
    pub language: String,
    pub keyterms: Vec<String>,
    pub replacements: Vec<String>,
    pub no_verbatim: bool,
    pub mute_during_recording: bool,
    pub recording_start_sound_volume: u16,
    pub overlay_position: String,
    #[serde(rename = "play_recording_start_sound", skip_serializing)]
    pub legacy_play_recording_start_sound: Option<bool>,
    pub openai_model: String,
    pub elevenlabs_model: String,
    pub groq_model: String,
    #[serde(skip)]
    pub api_key: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            shortcut: "right_option".into(),
            recording_control: "hold".into(),
            recording_time_limit_minutes: default_recording_limit(),
            log_retention_hours: default_log_retention(),
            auto_send: true,
            language: "ko".into(),
            keyterms: Vec::new(),
            replacements: Vec::new(),
            no_verbatim: true,
            mute_during_recording: true,
            recording_start_sound_volume: 100,
            overlay_position: crate::overlay::DEFAULT_POSITION.into(),
            legacy_play_recording_start_sound: None,
            openai_model: "gpt-transcribe".into(),
            elevenlabs_model: "scribe_v2".into(),
            groq_model: "whisper-large-v3-turbo".into(),
            api_key: String::new(),
        }
    }
}

pub fn directory() -> PathBuf {
    env::var_os("APPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(|| env::current_dir().unwrap_or_default())
        .join("keyscribe")
}

impl Settings {
    pub fn load() -> Self {
        let directory = directory();
        let mut value = fs::read_to_string(directory.join("config.toml"))
            .ok()
            .and_then(|text| Self::from_config(&text))
            .unwrap_or_default();
        if let Ok(text) = fs::read_to_string(directory.join("user_config.json")) {
            if let Ok(json) = serde_json::from_str::<serde_json::Value>(&text) {
                value.api_key = json
                    .get("api_key")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .into();
            }
        }
        if value.api_key.is_empty() {
            value.api_key = env::var("ELEVENLABS_API_KEY")
                .or_else(|_| env::var("GROQ_API_KEY"))
                .or_else(|_| env::var("OPENAI_API_KEY"))
                .unwrap_or_default();
        }
        value
    }

    pub fn save(&self) -> io::Result<()> {
        let directory = directory();
        fs::create_dir_all(&directory)?;
        let config = toml::to_string_pretty(self).map_err(io::Error::other)?;
        fs::write(directory.join("config.toml"), config)?;
        let user = serde_json::json!({"api_key": self.api_key});
        fs::write(
            directory.join("user_config.json"),
            serde_json::to_vec_pretty(&user).map_err(io::Error::other)?,
        )?;
        Ok(())
    }

    pub fn provider(&self) -> Option<Provider> {
        Provider::from_api_key(&self.api_key)
    }

    /// 전사 결과를 붙여넣기 직전에 치환 규칙대로 고친다. 규칙은 적힌 순서대로 적용된다.
    pub fn apply_replacements(&self, text: &str) -> String {
        self.replacements
            .iter()
            .filter_map(|rule| parse_replacement(rule))
            .fold(text.to_owned(), |result, (from, to)| {
                result.replace(from, to)
            })
    }

    fn from_config(text: &str) -> Option<Self> {
        let mut value = toml::from_str::<Self>(text).ok()?;
        let has_volume = toml::from_str::<toml::Table>(text)
            .ok()?
            .contains_key("recording_start_sound_volume");
        if !has_volume && value.legacy_play_recording_start_sound == Some(false) {
            value.recording_start_sound_volume = 0;
        }
        value.recording_start_sound_volume = value.recording_start_sound_volume.min(200);
        if !matches!(value.recording_time_limit_minutes, 10 | 20 | 30 | 60) {
            value.recording_time_limit_minutes = default_recording_limit();
        }
        if !crate::overlay::POSITIONS
            .iter()
            .any(|(code, _)| *code == value.overlay_position)
        {
            value.overlay_position = crate::overlay::DEFAULT_POSITION.into();
        }
        value.legacy_play_recording_start_sound = None;
        Some(value)
    }
}

/// 설정 화면에 보이는 `찾을 말 => 바꿀 말` 한 줄을 좌우 쌍으로 나눈다.
/// 사용자가 화살표를 `->`로 쓰는 경우도 같은 뜻으로 받아 준다.
pub fn parse_replacement(line: &str) -> Option<(&str, &str)> {
    let (from, to) = ["=>", "->"]
        .iter()
        .find_map(|separator| line.split_once(separator))?;
    let from = from.trim();
    (!from.is_empty()).then(|| (from, to.trim()))
}

/// 로그·녹음 원본 보존 기간(시간)과 설정 화면 표시 이름.
pub const LOG_RETENTION_OPTIONS: [(u32, &str); 4] =
    [(1, "1시간"), (24, "1일"), (168, "7일"), (720, "30일")];

fn default_log_retention() -> u32 {
    168
}

fn deserialize_log_retention<'de, D>(deserializer: D) -> Result<u32, D::Error>
where
    D: Deserializer<'de>,
{
    let value = toml::Value::deserialize(deserializer)?;
    Ok(value
        .as_integer()
        .and_then(|hours| u32::try_from(hours).ok())
        .filter(|hours| {
            LOG_RETENTION_OPTIONS
                .iter()
                .any(|(option, _)| option == hours)
        })
        .unwrap_or_else(default_log_retention))
}

fn default_recording_limit() -> u16 {
    30
}

fn deserialize_recording_limit<'de, D>(deserializer: D) -> Result<u16, D::Error>
where
    D: Deserializer<'de>,
{
    let value = toml::Value::deserialize(deserializer)?;
    Ok(value
        .as_integer()
        .and_then(|minutes| u16::try_from(minutes).ok())
        .filter(|minutes| matches!(minutes, 10 | 20 | 30 | 60))
        .unwrap_or_else(default_recording_limit))
}

#[cfg(test)]
mod tests {
    use super::{parse_replacement, Settings};

    #[test]
    fn replacement_lines_accept_both_arrows_and_drop_broken_ones() {
        assert_eq!(
            parse_replacement("비디오 스튜 => VideoStew"),
            Some(("비디오 스튜", "VideoStew"))
        );
        assert_eq!(parse_replacement("지피티->GPT"), Some(("지피티", "GPT")));
        assert_eq!(parse_replacement("지울 말 =>"), Some(("지울 말", "")));
        assert_eq!(parse_replacement("=> 바꿀 말"), None);
        assert_eq!(parse_replacement("화살표 없음"), None);
    }

    #[test]
    fn replacements_apply_in_order_before_pasting() {
        let settings = Settings::from_config(
            "replacements = [\"비디오 스튜 => VideoStew\", \"VideoStew => 비디오스튜\"]",
        )
        .unwrap();
        assert_eq!(
            settings.apply_replacements("오늘 비디오 스튜 소식"),
            "오늘 비디오스튜 소식"
        );
        assert_eq!(settings.apply_replacements("고칠 게 없다"), "고칠 게 없다");
    }

    #[test]
    fn migrates_disabled_start_sound_to_zero_volume() {
        let settings = Settings::from_config("play_recording_start_sound = false").unwrap();
        assert_eq!(settings.recording_start_sound_volume, 0);
    }

    #[test]
    fn explicit_volume_overrides_legacy_checkbox() {
        let settings = Settings::from_config(
            "play_recording_start_sound = false\nrecording_start_sound_volume = 150",
        )
        .unwrap();
        assert_eq!(settings.recording_start_sound_volume, 150);
    }

    #[test]
    fn log_retention_defaults_to_a_week_and_rejects_unknown_values() {
        let hours = |text| Settings::from_config(text).unwrap().log_retention_hours;
        assert_eq!(hours(""), 168);
        assert_eq!(hours("log_retention_hours = 720"), 720);
        assert_eq!(hours("log_retention_hours = 5"), 168);
        assert_eq!(hours("log_retention_hours = \"long\""), 168);
    }

    #[test]
    fn overlay_position_defaults_and_rejects_unknown_values() {
        assert_eq!(
            Settings::from_config("").unwrap().overlay_position,
            "bottom_center"
        );
        assert_eq!(
            Settings::from_config("overlay_position = \"top_right\"")
                .unwrap()
                .overlay_position,
            "top_right"
        );
        assert_eq!(
            Settings::from_config("overlay_position = \"somewhere\"")
                .unwrap()
                .overlay_position,
            "bottom_center"
        );
    }

    #[test]
    fn recording_limit_defaults_and_rejects_invalid_values() {
        assert_eq!(
            Settings::from_config("")
                .unwrap()
                .recording_time_limit_minutes,
            30
        );
        assert_eq!(
            Settings::from_config("recording_time_limit_minutes = 60")
                .unwrap()
                .recording_time_limit_minutes,
            60
        );
        assert_eq!(
            Settings::from_config("recording_time_limit_minutes = 45")
                .unwrap()
                .recording_time_limit_minutes,
            30
        );
        assert_eq!(
            Settings::from_config("recording_time_limit_minutes = \"long\"")
                .unwrap()
                .recording_time_limit_minutes,
            30
        );
    }
}
