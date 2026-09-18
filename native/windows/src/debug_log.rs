use std::{
    env,
    fs::{self, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    sync::{
        mpsc::{sync_channel, RecvTimeoutError, SyncSender},
        OnceLock,
    },
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

static SENDER: OnceLock<Option<SyncSender<String>>> = OnceLock::new();
const RETENTION_MS: u128 = 24 * 60 * 60 * 1000;

fn now_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|time| time.as_millis())
        .unwrap_or_default()
}

fn prune(path: &Path) {
    let Ok(contents) = fs::read_to_string(path) else {
        return;
    };
    let cutoff = now_ms().saturating_sub(RETENTION_MS);
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

pub fn init() {
    SENDER.get_or_init(|| {
        let path = path();
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        let (sender, receiver) = sync_channel::<String>(1024);
        thread::spawn(move || {
            prune(&path);
            let mut last_prune = Instant::now();
            loop {
                match receiver.recv_timeout(Duration::from_secs(60)) {
                    Ok(message) => {
                        if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(&path) {
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
        .unwrap_or_else(|| crate::settings::directory().join("debug.log"))
}

pub fn log(message: impl FnOnce() -> String) {
    if let Some(Some(sender)) = SENDER.get() {
        let _ = sender.try_send(message());
    }
}
