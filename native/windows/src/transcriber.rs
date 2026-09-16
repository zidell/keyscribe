use crate::settings::Settings;
use serde_json::Value;
use std::{ffi::c_void, fs::File, io::Read, path::Path, ptr};
use windows_sys::Win32::Networking::WinHttp::*;

struct Internet(*mut c_void);

impl Internet {
    fn new(handle: *mut c_void) -> Result<Self, String> {
        if handle.is_null() {
            Err(std::io::Error::last_os_error().to_string())
        } else {
            Ok(Self(handle))
        }
    }
}

impl Drop for Internet {
    fn drop(&mut self) {
        unsafe {
            WinHttpCloseHandle(self.0);
        }
    }
}

fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(Some(0)).collect()
}

enum Body<'a> {
    Empty,
    File {
        prefix: &'a [u8],
        path: &'a Path,
        suffix: &'a [u8],
    },
}

unsafe fn write_data(handle: *mut c_void, data: &[u8]) -> Result<(), String> {
    for chunk in data.chunks(64 * 1024) {
        let mut written = 0u32;
        if WinHttpWriteData(
            handle,
            chunk.as_ptr().cast(),
            chunk.len() as u32,
            &mut written,
        ) == 0
        {
            return Err(std::io::Error::last_os_error().to_string());
        }
        if written as usize != chunk.len() {
            return Err("요청 데이터를 모두 보내지 못했습니다.".into());
        }
    }
    Ok(())
}

fn request(
    host: &str,
    path: &str,
    method: &str,
    key: &str,
    openai: bool,
    body: Body<'_>,
    content_type: Option<&str>,
) -> Result<(u32, Vec<u8>), String> {
    unsafe {
        let session = Internet::new(WinHttpOpen(
            wide("KeyScribe/0.1").as_ptr(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            ptr::null(),
            ptr::null(),
            0,
        ))?;
        WinHttpSetTimeouts(session.0, 10_000, 10_000, 60_000, 60_000);
        let connection = Internet::new(WinHttpConnect(
            session.0,
            wide(host).as_ptr(),
            INTERNET_DEFAULT_HTTPS_PORT,
            0,
        ))?;
        let handle = Internet::new(WinHttpOpenRequest(
            connection.0,
            wide(method).as_ptr(),
            wide(path).as_ptr(),
            ptr::null(),
            ptr::null(),
            ptr::null(),
            WINHTTP_FLAG_SECURE,
        ))?;
        let mut headers = if openai {
            format!("Authorization: Bearer {key}\r\n")
        } else {
            format!("xi-api-key: {key}\r\n")
        };
        if let Some(content_type) = content_type {
            headers.push_str(&format!("Content-Type: {content_type}\r\n"));
        }
        let header = wide(&headers);
        let length = match &body {
            Body::Empty => 0,
            Body::File {
                prefix,
                path,
                suffix,
            } => {
                let size = std::fs::metadata(path).map_err(|e| e.to_string())?.len();
                u32::try_from(prefix.len() as u64 + size + suffix.len() as u64)
                    .map_err(|_| "녹음 파일이 너무 큽니다".to_string())?
            }
        };
        if WinHttpSendRequest(
            handle.0,
            header.as_ptr(),
            (header.len() - 1) as u32,
            ptr::null(),
            0,
            length,
            0,
        ) == 0
        {
            return Err(std::io::Error::last_os_error().to_string());
        }
        if let Body::File {
            prefix,
            path,
            suffix,
        } = body
        {
            write_data(handle.0, prefix)?;
            let mut file = File::open(path).map_err(|e| e.to_string())?;
            let mut chunk = [0u8; 64 * 1024];
            loop {
                let count = file.read(&mut chunk).map_err(|e| e.to_string())?;
                if count == 0 {
                    break;
                }
                write_data(handle.0, &chunk[..count])?;
            }
            write_data(handle.0, suffix)?;
        }
        if WinHttpReceiveResponse(handle.0, ptr::null_mut()) == 0 {
            return Err(std::io::Error::last_os_error().to_string());
        }
        let mut status = 0u32;
        let mut size = std::mem::size_of::<u32>() as u32;
        if WinHttpQueryHeaders(
            handle.0,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            ptr::null(),
            (&mut status as *mut u32).cast(),
            &mut size,
            ptr::null_mut(),
        ) == 0
        {
            return Err(std::io::Error::last_os_error().to_string());
        }
        let mut result = Vec::new();
        loop {
            let mut chunk = [0u8; 8192];
            let mut read = 0u32;
            if WinHttpReadData(
                handle.0,
                chunk.as_mut_ptr().cast(),
                chunk.len() as u32,
                &mut read,
            ) == 0
            {
                return Err(std::io::Error::last_os_error().to_string());
            }
            if read == 0 {
                break;
            }
            result.extend_from_slice(&chunk[..read as usize]);
            if result.len() > 4 * 1024 * 1024 {
                return Err("서버 응답이 너무 큽니다".into());
            }
        }
        Ok((status, result))
    }
}

fn multipart(settings: &Settings, boundary: &str) -> (Vec<u8>, Vec<u8>) {
    let mut result = Vec::with_capacity(1024);
    let mut field = |name: &str, value: &str| {
        result.extend_from_slice(format!("--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n\r\n{value}\r\n").as_bytes());
    };
    if settings.openai() {
        field("model", &settings.openai_model);
        field("language", &settings.language);
        if !settings.keyterms.is_empty() {
            field(
                "prompt",
                &format!(
                    "이 녹음에는 다음 용어가 포함됩니다: {}.",
                    settings.keyterms.join(", ")
                ),
            );
        }
    } else {
        field("model_id", &settings.elevenlabs_model);
        field("language_code", &settings.language);
        for term in &settings.keyterms {
            field("keyterms", term);
        }
        if matches!(
            settings.elevenlabs_model.as_str(),
            "scribe_v2" | "scribe_v2_medical"
        ) {
            field(
                "no_verbatim",
                if settings.no_verbatim {
                    "true"
                } else {
                    "false"
                },
            );
        }
    }
    result.extend_from_slice(format!("--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\nContent-Type: audio/wav\r\n\r\n").as_bytes());
    (result, format!("\r\n--{boundary}--\r\n").into_bytes())
}

pub fn transcribe(wav: &Path, settings: &Settings) -> Result<String, String> {
    if settings.api_key.is_empty() {
        return Err("API 키를 설정해 주세요".into());
    }
    if settings.openai()
        && std::fs::metadata(wav).map_err(|e| e.to_string())?.len() > 24 * 1024 * 1024
    {
        return Err("OpenAI 녹음 크기 제한(24 MB)을 초과했습니다".into());
    }
    let (host, path) = if settings.openai() {
        ("api.openai.com", "/v1/audio/transcriptions")
    } else {
        ("api.elevenlabs.io", "/v1/speech-to-text")
    };
    let boundary = format!("KeyScribe-{}", std::process::id());
    let (prefix, suffix) = multipart(settings, &boundary);
    let content_type = format!("multipart/form-data; boundary={boundary}");
    let mut last_error = String::new();
    for _attempt in 0..2 {
        match request(
            host,
            path,
            "POST",
            &settings.api_key,
            settings.openai(),
            Body::File {
                prefix: &prefix,
                path: wav,
                suffix: &suffix,
            },
            Some(&content_type),
        ) {
            Ok((status, response)) if (200..300).contains(&status) => {
                let json: Value = serde_json::from_slice(&response)
                    .map_err(|_| "전사 응답을 읽을 수 없습니다".to_string())?;
                let text = json
                    .get("text")
                    .and_then(Value::as_str)
                    .ok_or("전사 응답에 텍스트가 없습니다")?;
                return Ok(clean_text(text));
            }
            Ok((status, response)) => {
                let detail = String::from_utf8_lossy(&response);
                last_error = format!(
                    "전사 실패 (HTTP {status}): {}",
                    detail.chars().take(300).collect::<String>()
                );
                if !(500..600).contains(&status) {
                    break;
                }
            }
            Err(error) => {
                last_error = error;
            }
        }
    }
    Err(last_error)
}

pub fn models(settings: &Settings) -> Result<Vec<String>, String> {
    let (host, path) = if settings.openai() {
        ("api.openai.com", "/v1/models")
    } else {
        ("api.elevenlabs.io", "/v1/models")
    };
    let (status, response) = request(
        host,
        path,
        "GET",
        &settings.api_key,
        settings.openai(),
        Body::Empty,
        None,
    )?;
    if !(200..300).contains(&status) {
        return Err(format!("모델 목록 요청 실패 (HTTP {status})"));
    }
    let json: Value = serde_json::from_slice(&response).map_err(|e| e.to_string())?;
    let items = if settings.openai() {
        json.get("data").and_then(Value::as_array)
    } else {
        json.as_array()
    }
    .ok_or("모델 목록을 읽을 수 없습니다")?;
    let key = if settings.openai() { "id" } else { "model_id" };
    let mut result: Vec<String> = items
        .iter()
        .filter_map(|item| item.get(key).and_then(Value::as_str))
        .filter(|name| {
            let supported = if settings.openai() {
                name.contains("transcribe") || *name == "whisper-1"
            } else {
                name.starts_with("scribe")
            };
            supported && !has_date_suffix(name)
        })
        .map(str::to_owned)
        .collect();
    result.sort();
    result.dedup();
    Ok(result)
}

fn has_date_suffix(name: &str) -> bool {
    let Some(suffix) = name.get(name.len().saturating_sub(11)..) else {
        return false;
    };
    let bytes = suffix.as_bytes();
    bytes.len() == 11
        && bytes[0] == b'-'
        && bytes[5] == b'-'
        && bytes[8] == b'-'
        && bytes
            .iter()
            .enumerate()
            .all(|(i, c)| [0, 5, 8].contains(&i) || c.is_ascii_digit())
}

fn clean_text(text: &str) -> String {
    let mut result = String::new();
    let mut remaining = text;
    while let Some(start) = remaining.find('[') {
        result.push_str(&remaining[..start]);
        let after_open = &remaining[start + 1..];
        if let Some(end) = after_open.find(']') {
            remaining = &after_open[end + 1..];
        } else {
            remaining = &remaining[start..];
            break;
        }
    }
    result.push_str(remaining);
    result.split_whitespace().collect::<Vec<_>>().join(" ")
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cleans_transcript() {
        assert_eq!(clean_text("안녕 [noise]  하세요"), "안녕 하세요");
        assert_eq!(clean_text("안녕 [unfinished"), "안녕 [unfinished");
    }
    #[test]
    fn detects_dated_model() {
        assert!(has_date_suffix("gpt-transcribe-2025-01-01"));
        assert!(!has_date_suffix("gpt-transcribe"));
    }
    #[test]
    fn multipart_uses_only_the_active_provider_fields() {
        let mut settings = Settings::default();
        settings.api_key = "sk-openai-example".into();
        settings.keyterms = vec!["KeyScribe".into()];
        let (openai, suffix) = multipart(&settings, "boundary");
        let openai = String::from_utf8(openai).unwrap();
        assert!(openai.contains("name=\"model\"\r\n\r\ngpt-transcribe"));
        assert!(openai.contains("name=\"prompt\""));
        assert!(!openai.contains("name=\"model_id\""));
        assert_eq!(suffix, b"\r\n--boundary--\r\n");

        settings.api_key = "sk_elevenlabs_example".into();
        let (elevenlabs, _) = multipart(&settings, "boundary");
        let elevenlabs = String::from_utf8(elevenlabs).unwrap();
        assert!(elevenlabs.contains("name=\"model_id\"\r\n\r\nscribe_v2"));
        assert!(elevenlabs.contains("name=\"no_verbatim\""));
        assert!(!elevenlabs.contains("name=\"prompt\""));
    }
}
