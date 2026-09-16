use serde::{Deserialize, Serialize};
use std::{env, fs, io, path::PathBuf};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub shortcut: String,
    pub recording_control: String,
    pub auto_send: bool,
    pub language: String,
    pub keyterms: Vec<String>,
    pub no_verbatim: bool,
    pub mute_during_recording: bool,
    pub openai_model: String,
    pub elevenlabs_model: String,
    #[serde(skip)]
    pub api_key: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            shortcut: "right_option".into(),
            recording_control: "hold".into(),
            auto_send: true,
            language: "ko".into(),
            keyterms: Vec::new(),
            no_verbatim: true,
            mute_during_recording: true,
            openai_model: "gpt-transcribe".into(),
            elevenlabs_model: "scribe_v2".into(),
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
            .and_then(|text| toml::from_str::<Self>(&text).ok())
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
            value.api_key = env::var("ELEVENLABS_API_KEY").unwrap_or_default();
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

    pub fn openai(&self) -> bool {
        self.api_key.starts_with("sk-")
    }
}
