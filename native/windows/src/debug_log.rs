use std::{
    env,
    fs::{self, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicU64, Ordering},
        mpsc::{sync_channel, RecvTimeoutError, SyncSender},
        OnceLock,
    },
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

static SENDER: OnceLock<Option<SyncSender<String>>> = OnceLock::new();
/// 보존 기간(시간). 설정을 읽기 전인 0이면 아무것도 지우지 않는다.
static RETENTION_HOURS: AtomicU64 = AtomicU64::new(0);

fn retention_ms() -> u128 {
    u128::from(RETENTION_HOURS.load(Ordering::Relaxed)) * 60 * 60 * 1000
}

/// 로그와 녹음 원본의 보존 기간을 바꾼다. 다음 정리(1분 간격) 때 반영된다.
pub fn set_retention(hours: u32) {
    RETENTION_HOURS.store(u64::from(hours), Ordering::Relaxed);
}

fn now_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|time| time.as_millis())
        .unwrap_or_default()
}

fn prune(path: &Path) {
    let retention = retention_ms();
    if retention == 0 {
        return;
    }
    prune_recordings(&directory(), retention);
    let Ok(contents) = fs::read_to_string(path) else {
        return;
    };
    let cutoff = now_ms().saturating_sub(retention);
    let kept = contents
        .lines()
        .filter(|line| {
            line.split_once(' ')
                .and_then(|(timestamp, _)| timestamp.parse::<u128>().ok())
                .is_some_and(|timestamp| timestamp >= cutoff)
        })
        .collect::<Vec<_>>()
        .join("\n");
    let _ = fs::write(path, if kept.is_empty() { kept } else { kept + "\n" });
}

fn prune_recordings(directory: &Path, retention_ms: u128) {
    let Ok(entries) = fs::read_dir(directory) else {
        return;
    };
    let retention = Duration::from_millis(retention_ms as u64);
    for entry in entries.flatten() {
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if !name.starts_with("recording-") || !name.ends_with(".wav") {
            continue;
        }
        let expired = entry
            .metadata()
            .and_then(|meta| meta.modified())
            .ok()
            .and_then(|modified| modified.elapsed().ok())
            .is_some_and(|age| age >= retention);
        if expired {
            let _ = fs::remove_file(entry.path());
        }
    }
}

pub fn init() {
    SENDER.get_or_init(|| {
        let path = ensure_file().unwrap_or_else(|_| path());
        let (sender, receiver) = sync_channel::<String>(1024);
        thread::spawn(move || {
            let mut last_prune = Instant::now();
            loop {
                match receiver.recv_timeout(Duration::from_secs(60)) {
                    Ok(message) => {
                        if let Ok(mut file) =
                            OpenOptions::new().create(true).append(true).open(&path)
                        {
                            let _ = writeln!(file, "{} {message}", now_ms());
                        }
                    }
                    Err(RecvTimeoutError::Timeout) => {}
                    Err(RecvTimeoutError::Disconnected) => break,
                }
                if last_prune.elapsed() >= Duration::from_secs(60) {
                    prune(&path);
                    last_prune = Instant::now();
                }
            }
        });
        Some(sender)
    });
}

pub fn path() -> PathBuf {
    env::var_os("KEYSCRIBE_DEBUG_LOG")
        .map(PathBuf::from)
        .unwrap_or_else(|| crate::settings::directory().join("logs").join("debug.log"))
}

/// 로그와 녹음 원본을 함께 두는 폴더.
pub fn directory() -> PathBuf {
    path()
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(crate::settings::directory)
}

/// 전사가 실패해도 다시 쓸 수 있게 녹음 원본을 남길 경로. 로그와 같은 보존 기간이 지나면 지운다.
pub fn new_recording_path() -> std::io::Result<PathBuf> {
    use windows_sys::Win32::System::SystemInformation::GetLocalTime;
    let directory = directory();
    fs::create_dir_all(&directory)?;
    let mut time = unsafe { std::mem::zeroed() };
    unsafe { GetLocalTime(&mut time) };
    Ok(directory.join(format!(
        "recording-{:04}{:02}{:02}-{:02}{:02}{:02}-{:03}.wav",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds
    )))
}

pub fn ensure_file() -> std::io::Result<PathBuf> {
    let path = path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    // 예전에는 앱 데이터 폴더 바로 아래에 로그를 두었다.
    if env::var_os("KEYSCRIBE_DEBUG_LOG").is_none() {
        let legacy = crate::settings::directory().join("debug.log");
        if legacy.exists() && fs::rename(&legacy, &path).is_err() {
            let _ = fs::remove_file(legacy);
        }
    }
    OpenOptions::new().create(true).append(true).open(&path)?;
    Ok(path)
}

pub fn log(message: impl FnOnce() -> String) {
    if let Some(Some(sender)) = SENDER.get() {
        let _ = sender.try_send(message());
    }
}
