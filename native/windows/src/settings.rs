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
    pub auto_send: bool,
    pub language: String,
    pub keyterms: Vec<String>,
    pub no_verbatim: bool,
    pub mute_during_recording: bool,
    pub recording_start_sound_volume: u16,
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
            auto_send: true,
            language: "ko".into(),
            keyterms: Vec::new(),
            no_verbatim: true,
            mute_during_recording: true,
            recording_start_sound_volume: 100,
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
        value.legacy_play_recording_start_sound = None;
        Some(value)
    }
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
    use super::Settings;

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
