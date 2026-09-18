use crate::{
    audio::Recording,
    mute, overlay,
    settings::{self, Settings},
    transcriber,
};
use std::{
    collections::HashMap,
    mem, ptr,
    sync::{
        atomic::{AtomicBool, AtomicIsize, AtomicU16, AtomicU32, AtomicU64, AtomicU8, Ordering},
        Arc,
    },
    time::{Duration, Instant},
};
use windows_sys::Win32::{
    Foundation::{
        CloseHandle, GlobalFree, HANDLE, HWND, LPARAM, LRESULT, POINT, WAIT_OBJECT_0, WPARAM,
    },
    Globalization::{GetLocaleInfoEx, LOCALE_SLOCALIZEDLANGUAGENAME},
    Graphics::Gdi::{GetStockObject, UpdateWindow, COLOR_BTNFACE, DEFAULT_GUI_FONT},
    Media::Audio::{PlaySoundW, SND_MEMORY, SND_NODEFAULT, SND_SYNC},
    System::{
        DataExchange::{CloseClipboard, EmptyClipboard, OpenClipboard, SetClipboardData},
        LibraryLoader::GetModuleHandleW,
        Memory::{GlobalAlloc, GlobalLock, GlobalUnlock, GMEM_MOVEABLE},
        Threading::{CreateEventW, WaitForSingleObject},
    },
    UI::{
        Controls::{
            InitCommonControlsEx, ICC_BAR_CLASSES, INITCOMMONCONTROLSEX, TBM_SETPOS,
            TBM_SETRANGEMAX, TBM_SETRANGEMIN, TBS_NOTICKS,
        },
        Input::KeyboardAndMouse::*,
        Shell::*,
        WindowsAndMessaging::*,
    },
};

const TRAY_MESSAGE: u32 = WM_APP + 1;
const KEY_MESSAGE: u32 = WM_APP + 2;
const DEV_STOP_TIMER: usize = 0x4b53;
static DEV_STOP_EVENT: AtomicIsize = AtomicIsize::new(0);
const RESULT_MESSAGE: u32 = WM_APP + 3;
const MODELS_MESSAGE: u32 = WM_APP + 4;
const RIGHT_ALT_TIMER: usize = 3;
const CHIME_FINISHED_MESSAGE: u32 = WM_APP + 5;
const AUTO_SEND_DOWN_TIMER: usize = 4;
const AUTO_SEND_UP_TIMER: usize = 5;
const TRACKBAR_GET_POSITION: u32 = WM_USER;
const ID_SETTINGS: usize = 101;
const ID_FOLDER: usize = 102;
const ID_EXIT: usize = 103;
const ID_RESTART: usize = 104;
const ID_SAVE: usize = 201;
const ID_CANCEL: usize = 202;
const ID_REFRESH: usize = 203;
const ID_API_KEY: usize = 204;
const ID_MODEL: usize = 205;
const ID_ELEVENLABS_KEY: usize = 206;
const ID_OPENAI_KEY: usize = 207;
const ID_LOG: usize = 208;
const SHORTCUTS: [(&str, &str, u16); 6] = [
    ("right_alt", "오른쪽 Alt", VK_RMENU),
    ("left_alt", "왼쪽 Alt", VK_LMENU),
    ("right_ctrl", "오른쪽 Ctrl", VK_RCONTROL),
    ("left_ctrl", "왼쪽 Ctrl", VK_LCONTROL),
    ("right_shift", "오른쪽 Shift", VK_RSHIFT),
    ("left_shift", "왼쪽 Shift", VK_LSHIFT),
];
static ROOT: AtomicIsize = AtomicIsize::new(0);
static TARGET_KEY: AtomicU16 = AtomicU16::new(VK_RMENU);
static NEXT_REQUEST: AtomicU64 = AtomicU64::new(1);
static TASKBAR_CREATED: AtomicU32 = AtomicU32::new(0);
// 0 = idle, 1 = waiting for a chord, 2 = recording, 3 = passing a chord through.
static RIGHT_ALT_STATE: AtomicU8 = AtomicU8::new(0);
static AUTO_STOPPED_ALT_HELD: AtomicBool = AtomicBool::new(false);
static RIGHT_ALT_DOWN_TIME: AtomicU32 = AtomicU32::new(0);

fn is_recording_key(physical: u32, target: u32) -> bool {
    physical == target
}

fn summarize_recent(text: &str) -> String {
    let normalized = text.split_whitespace().collect::<Vec<_>>().join(" ");
    let length = normalized.chars().count();
    if length <= 30 {
        return normalized;
    }
    let start: String = normalized.chars().take(15).collect();
    let end: String = normalized.chars().skip(length - 15).collect();
    format!("{start}…{end}")
}

#[cfg(test)]
mod shortcut_tests {
    use super::*;

    #[test]
    fn right_alt_does_not_accept_other_modifier_keys() {
        assert!(is_recording_key(VK_RMENU as u32, VK_RMENU as u32));
        assert!(!is_recording_key(VK_HANGUL as u32, VK_RMENU as u32));
        assert!(!is_recording_key(VK_LMENU as u32, VK_RMENU as u32));
    }
}

unsafe fn tray_icon() -> (HICON, bool) {
    let png = include_bytes!(concat!(env!("OUT_DIR"), "/keyscribe-tray.png"));
    let custom = CreateIconFromResourceEx(
        png.as_ptr(),
        png.len() as u32,
        1,
        0x0003_0000,
        32,
        32,
        LR_DEFAULTCOLOR,
    );
    if custom.is_null() {
        (LoadIconW(ptr::null_mut(), IDI_APPLICATION), false)
    } else {
        (custom, true)
    }
}

#[cfg(test)]
mod icon_tests {
    use super::*;

    #[test]
    fn custom_icon_loads() {
        unsafe {
            let (icon, owned) = tray_icon();
            assert!(!icon.is_null());
            assert!(owned, "Windows did not decode the KeyScribe PNG icon");
            DestroyIcon(icon);
        }
    }
}

fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(Some(0)).collect()
}
fn loword(value: usize) -> usize {
    value & 0xffff
}

fn language_options(selected: &str) -> Vec<(String, String)> {
    let mut options: Vec<(String, String)> =
        "af ar hy az be bs bg ca zh hr cs da nl en et fi fr gl de el he hi hu id it ja kn kk ko lv lt mk ms mr mi ne no fa pl pt ro ru sr sk sl es sw sv tl ta th tr uk ur vi cy"
            .split_whitespace().map(|code| {
                let name = unsafe {
                    let mut buffer = [0u16; 128];
                    let count = GetLocaleInfoEx(wide(code).as_ptr(), LOCALE_SLOCALIZEDLANGUAGENAME,
                                                buffer.as_mut_ptr(), buffer.len() as i32);
                    if count > 1 { String::from_utf16_lossy(&buffer[..count as usize - 1]) }
                    else { code.to_owned() }
                };
                (code.to_owned(), name)
            }).collect();
    options.sort_by(|left, right| left.1.cmp(&right.1));
    if !options.iter().any(|(code, _)| code == selected) {
        options.insert(0, (selected.to_owned(), selected.to_owned()));
    }
    options
}

struct App {
    tray: NOTIFYICONDATAW,
    owned_icon: bool,
    hook: HHOOK,
    overlay: HWND,
    overlay_expires: Option<Instant>,
    settings: Settings,
    model_cache: HashMap<String, Vec<String>>,
    dialog: HWND,
    recording: Option<Recording>,
    recording_started_at: Option<Instant>,
    recording_deadline: Option<Instant>,
    recording_seconds_shown: Option<u64>,
    limit_sound_playing: Arc<AtomicBool>,
    transcription_cancelled: Option<Arc<AtomicBool>>,
    mute_before: Option<mute::MuteState>,
    pressed: bool,
    generation: u64,
    transcribing: bool,
    status: String,
    last_text: String,
}

struct Dialog {
    root: HWND,
    api_key: HWND,
    model: HWND,
    model_hint: HWND,
    refresh: HWND,
    shown_provider: Option<bool>,
    openai_model: String,
    elevenlabs_model: String,
    request_id: u64,
    pending_key: Option<String>,
    language: HWND,
    language_codes: Vec<String>,
    shortcut: HWND,
    mode: HWND,
    recording_time_limit: HWND,
    keyterms: HWND,
    no_verbatim: HWND,
    mute: HWND,
    recording_start_sound_volume: HWND,
    recording_start_sound_value: HWND,
    auto_send: HWND,
}

struct ResultMessage {
    generation: u64,
    result: Result<String, String>,
}
struct ModelsMessage {
    result: Result<Vec<String>, String>,
    request_id: u64,
    key: String,
    preserve: bool,
}

pub fn run() -> Result<(), String> {
    crate::debug_log::init();
    crate::debug_log::log(|| "app start".into());
    unsafe {
        mute::initialize();
        crate::debug_log::log(|| "audio COM initialized".into());
        let common_controls = INITCOMMONCONTROLSEX {
            dwSize: mem::size_of::<INITCOMMONCONTROLSEX>() as u32,
            dwICC: ICC_BAR_CLASSES,
        };
        if InitCommonControlsEx(&common_controls) == 0 {
            return Err("설정 슬라이더를 초기화하지 못했습니다".into());
        }
        let instance = GetModuleHandleW(ptr::null());
        if instance.is_null() {
            return Err("Windows 모듈을 찾을 수 없습니다".into());
        }
        let root_class = wide("KeyScribeNativeRoot");
        let settings_class = wide("KeyScribeNativeSettings");
        let (icon, owned_icon) = tray_icon();
        TASKBAR_CREATED.store(
            RegisterWindowMessageW(wide("TaskbarCreated").as_ptr()),
            Ordering::Relaxed,
        );
        let root_definition = WNDCLASSW {
            style: 0,
            lpfnWndProc: Some(root_proc),
            cbClsExtra: 0,
            cbWndExtra: 0,
            hInstance: instance,
            hIcon: icon,
            hCursor: LoadCursorW(ptr::null_mut(), IDC_ARROW),
            hbrBackground: ptr::null_mut(),
            lpszMenuName: ptr::null(),
            lpszClassName: root_class.as_ptr(),
        };
        let settings_definition = WNDCLASSW {
            lpfnWndProc: Some(dialog_proc),
            lpszClassName: settings_class.as_ptr(),
            hbrBackground: (COLOR_BTNFACE as usize + 1) as _,
            ..root_definition
        };
        if RegisterClassW(&root_definition) == 0 || RegisterClassW(&settings_definition) == 0 {
            return Err("Windows 창 클래스를 등록하지 못했습니다".into());
        }
        crate::debug_log::log(|| "window classes registered".into());
        if !overlay::register(instance) {
            return Err("녹음 상태 창을 등록하지 못했습니다".into());
        }
        let hwnd = CreateWindowExW(
            0,
            root_class.as_ptr(),
            wide("KeyScribe").as_ptr(),
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            0,
            0,
            ptr::null_mut(),
            ptr::null_mut(),
            instance,
            ptr::null(),
        );
        if hwnd.is_null() {
            return Err("KeyScribe 창을 만들지 못했습니다".into());
        }
        crate::debug_log::log(|| "root window created".into());
        let settings = Settings::load();
        TARGET_KEY.store(shortcut_key(&settings.shortcut), Ordering::SeqCst);
        let mut tray = NOTIFYICONDATAW::default();
        tray.cbSize = mem::size_of::<NOTIFYICONDATAW>() as u32;
        tray.hWnd = hwnd;
        tray.uID = 1;
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray.uCallbackMessage = TRAY_MESSAGE;
        tray.hIcon = icon;
        write_wide(&mut tray.szTip, "KeyScribe · 준비됨");
        let overlay = overlay::create(instance, hwnd);
        let state = Box::new(App {
            tray,
            owned_icon,
            hook: ptr::null_mut(),
            overlay,
            overlay_expires: None,
            settings,
            model_cache: HashMap::new(),
            dialog: ptr::null_mut(),
            recording: None,
            recording_started_at: None,
            recording_deadline: None,
            recording_seconds_shown: None,
            limit_sound_playing: Arc::new(AtomicBool::new(false)),
            transcription_cancelled: None,
            mute_before: None,
            pressed: false,
            generation: 0,
            transcribing: false,
            status: "준비됨".into(),
            last_text: "없음".into(),
        });
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, Box::into_raw(state) as isize);
        ROOT.store(hwnd as isize, Ordering::SeqCst);
        if std::env::var_os("KEYSCRIBE_DEBUG_LOG").is_some() {
            let event_name = wide(&format!("Local\\KeyScribeDevStop-{}", std::process::id()));
            let event = CreateEventW(ptr::null(), 0, 0, event_name.as_ptr());
            if !event.is_null() {
                DEV_STOP_EVENT.store(event as isize, Ordering::SeqCst);
                SetTimer(hwnd, DEV_STOP_TIMER, 250, None);
                crate::debug_log::log(|| "development shutdown event ready".into());
            } else {
                crate::debug_log::log(|| "development shutdown event creation failed".into());
            }
        }
        if Shell_NotifyIconW(NIM_ADD, &app(hwnd).tray) == 0 {
            DestroyWindow(hwnd);
            return Err("트레이 아이콘을 만들지 못했습니다".into());
        }
        crate::debug_log::log(|| "tray icon added".into());
        app(hwnd).hook = SetWindowsHookExW(WH_KEYBOARD_LL, Some(keyboard_hook), instance, 0);
        if app(hwnd).hook.is_null() {
            DestroyWindow(hwnd);
            return Err("전역 단축키를 등록하지 못했습니다".into());
        }
        crate::debug_log::log(|| "keyboard hook installed".into());
        if app(hwnd).settings.api_key.is_empty() {
            show_settings(hwnd);
        }
        let mut message: MSG = mem::zeroed();
        while GetMessageW(&mut message, ptr::null_mut(), 0, 0) > 0 {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Ok(())
    }
}

unsafe fn app(hwnd: HWND) -> &'static mut App {
    &mut *(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut App)
}

unsafe extern "system" fn keyboard_hook(code: i32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    if code >= 0
        && (wparam == WM_KEYDOWN as usize
            || wparam == WM_SYSKEYDOWN as usize
            || wparam == WM_KEYUP as usize
            || wparam == WM_SYSKEYUP as usize)
    {
        let key = &*(lparam as *const KBDLLHOOKSTRUCT);
        if key.flags & LLKHF_INJECTED != 0 {
            if key.vkCode == VK_RMENU as u32
                || key.vkCode == VK_MENU as u32
                || key.vkCode == VK_ESCAPE as u32
            {
                crate::debug_log::log(|| {
                    format!(
                        "key injected vk={} scan={} flags=0x{:x} ignored",
                        key.vkCode, key.scanCode, key.flags
                    )
                });
            }
            return CallNextHookEx(ptr::null_mut(), code, wparam, lparam);
        }
        let physical = match key.vkCode as u16 {
            VK_MENU => {
                if key.flags & LLKHF_EXTENDED != 0 {
                    VK_RMENU
                } else {
                    VK_LMENU
                }
            }
            VK_CONTROL => {
                if key.flags & LLKHF_EXTENDED != 0 {
                    VK_RCONTROL
                } else {
                    VK_LCONTROL
                }
            }
            VK_SHIFT => {
                if key.scanCode == 0x36 {
                    VK_RSHIFT
                } else {
                    VK_LSHIFT
                }
            }
            other => other,
        } as u32;
        let down = wparam == WM_KEYDOWN as usize || wparam == WM_SYSKEYDOWN as usize;
        let target = TARGET_KEY.load(Ordering::Relaxed) as u32;
        let recording_key = is_recording_key(physical, target);
        let root = ROOT.load(Ordering::Relaxed) as HWND;
        let alt_state = RIGHT_ALT_STATE.load(Ordering::Relaxed);
        if recording_key || physical == VK_RMENU as u32 || physical == VK_ESCAPE as u32 {
            crate::debug_log::log(|| {
                format!("key vk={} physical={} scan={} flags=0x{:x} down={} target={} alt_state={} root={}", key.vkCode, physical, key.scanCode, key.flags, down, target, alt_state, !root.is_null())
            });
        }
        if !root.is_null() && (target == VK_RMENU as u32 || alt_state != 0) {
            if is_recording_key(physical, VK_RMENU as u32) {
                if down {
                    if alt_state == 0 {
                        if SetTimer(root, RIGHT_ALT_TIMER, 100, None) == 0 {
                            crate::debug_log::log(|| "right_alt pass: timer failed".into());
                            return CallNextHookEx(ptr::null_mut(), code, wparam, lparam);
                        }
                        RIGHT_ALT_DOWN_TIME.store(key.time, Ordering::Relaxed);
                        RIGHT_ALT_STATE.store(1, Ordering::Relaxed);
                        crate::debug_log::log(|| "right_alt down suppressed; waiting 100ms".into());
                    }
                    return 1;
                }
                match alt_state {
                    1 => {
                        crate::debug_log::log(|| "right_alt tap: queue down/up".into());
                        KillTimer(root, RIGHT_ALT_TIMER);
                        PostMessageW(root, KEY_MESSAGE, VK_RMENU as usize, 1);
                        PostMessageW(root, KEY_MESSAGE, VK_RMENU as usize, 0);
                    }
                    2 => {
                        crate::debug_log::log(|| "right_alt hold: queue up".into());
                        PostMessageW(root, KEY_MESSAGE, VK_RMENU as usize, 0);
                    }
                    3 => {
                        crate::debug_log::log(|| "right_alt combo: replay up".into());
                        send_keys(&[(VK_RMENU, true)]);
                    }
                    _ => return CallNextHookEx(ptr::null_mut(), code, wparam, lparam),
                }
                RIGHT_ALT_STATE.store(0, Ordering::Relaxed);
                AUTO_STOPPED_ALT_HELD.store(false, Ordering::Relaxed);
                return 1;
            }
            if down && (alt_state == 1 || alt_state == 2) {
                // AltGr can generate a synthetic left Ctrl alongside right Alt.
                if physical == VK_LCONTROL as u32
                    && key
                        .time
                        .wrapping_sub(RIGHT_ALT_DOWN_TIME.load(Ordering::Relaxed))
                        <= 1
                {
                    return CallNextHookEx(ptr::null_mut(), code, wparam, lparam);
                }
                KillTimer(root, RIGHT_ALT_TIMER);
                RIGHT_ALT_STATE.store(3, Ordering::Relaxed);
                crate::debug_log::log(|| {
                    format!(
                        "right_alt combo: key={} prior_state={alt_state}; replay down",
                        physical
                    )
                });
                if alt_state == 2 {
                    if !AUTO_STOPPED_ALT_HELD.swap(false, Ordering::Relaxed) {
                        PostMessageW(root, KEY_MESSAGE, VK_ESCAPE as usize, 1);
                    }
                    PostMessageW(root, KEY_MESSAGE, VK_RMENU as usize, 0);
                }
                send_keys(&[(VK_RMENU, false)]);
                return CallNextHookEx(ptr::null_mut(), code, wparam, lparam);
            }
        }
        if recording_key || physical == VK_ESCAPE as u32 {
            if !root.is_null() {
                crate::debug_log::log(|| format!("key queued physical={physical} down={down}"));
                PostMessageW(root, KEY_MESSAGE, physical as usize, down as isize);
            }
        }
    }
    CallNextHookEx(ptr::null_mut(), code, wparam, lparam)
}

unsafe extern "system" fn root_proc(
    hwnd: HWND,
    message: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    if GetWindowLongPtrW(hwnd, GWLP_USERDATA) == 0 {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    match message {
        event if event == TASKBAR_CREATED.load(Ordering::Relaxed) => {
            Shell_NotifyIconW(NIM_ADD, &app(hwnd).tray);
            0
        }
        TRAY_MESSAGE => {
            match lparam as u32 {
                WM_RBUTTONUP | WM_CONTEXTMENU => tray_menu(hwnd),
                WM_LBUTTONDBLCLK => show_settings(hwnd),
                _ => {}
            }
            0
        }
        KEY_MESSAGE => {
            handle_key(hwnd, wparam as u32, lparam != 0);
            0
        }
        CHIME_FINISHED_MESSAGE => {
            if wparam as u64 == app(hwnd).generation
                && app(hwnd).recording.is_some()
                && app(hwnd).settings.mute_during_recording
            {
                app(hwnd).mute_before = mute::mute();
                crate::debug_log::log(|| {
                    format!("recording mute result={:?}", app(hwnd).mute_before)
                });
            }
            0
        }
        RESULT_MESSAGE => {
            let result = Box::from_raw(lparam as *mut ResultMessage);
            if result.generation == app(hwnd).generation && app(hwnd).transcribing {
                app(hwnd).transcribing = false;
                app(hwnd).transcription_cancelled = None;
                match result.result {
                    Ok(text) if !text.is_empty() => {
                        crate::debug_log::log(|| {
                            format!(
                                "transcription completed characters={}",
                                text.chars().count()
                            )
                        });
                        hide_overlay(hwnd);
                        app(hwnd).last_text = summarize_recent(&text);
                        match paste(hwnd, &text, app(hwnd).settings.auto_send) {
                            Ok(()) => set_status(hwnd, "완료"),
                            Err(error) => {
                                crate::debug_log::log(|| "paste failed".into());
                                set_status(hwnd, &format!("붙여넣기 실패: {error}"));
                            }
                        }
                    }
                    Ok(_) => {
                        crate::debug_log::log(|| "transcription completed with empty text".into());
                        hide_overlay(hwnd);
                        set_status(hwnd, "인식된 음성이 없습니다");
                    }
                    Err(error) => {
                        crate::debug_log::log(|| "transcription failed after retries".into());
                        set_status(hwnd, &error);
                        transient_overlay(hwnd, overlay::State::Failed);
                    }
                }
            }
            0
        }
        MODELS_MESSAGE => {
            let result = Box::from_raw(lparam as *mut ModelsMessage);
            if let Ok(models) = &result.result {
                if !models.is_empty() {
                    app(hwnd)
                        .model_cache
                        .insert(result.key.clone(), models.clone());
                }
            }
            let dialog = app(hwnd).dialog;
            if !dialog.is_null() {
                let controls = dialog_state(dialog);
                if result.request_id == controls.request_id
                    && text(controls.api_key).trim() == result.key
                {
                    controls.pending_key = None;
                    EnableWindow(controls.refresh, 1);
                    match result.result {
                        Ok(models) if !models.is_empty() => {
                            display_models(dialog, &models, result.preserve)
                        }
                        Ok(_) => {
                            SetWindowTextW(
                                controls.model_hint,
                                wide("사용 가능한 전사 모델이 없습니다.").as_ptr(),
                            );
                        }
                        Err(error) => {
                            SetWindowTextW(controls.model_hint, wide(&error).as_ptr());
                        }
                    }
                }
            }
            0
        }
        WM_COMMAND => {
            tray_command(hwnd, loword(wparam));
            0
        }
        WM_TIMER => {
            if wparam == DEV_STOP_TIMER {
                let event = DEV_STOP_EVENT.load(Ordering::Relaxed) as HANDLE;
                if !event.is_null() && WaitForSingleObject(event, 0) == WAIT_OBJECT_0 {
                    crate::debug_log::log(|| "development shutdown requested".into());
                    DestroyWindow(hwnd);
                }
            } else if wparam == 1 {
                if app(hwnd)
                    .recording_deadline
                    .is_some_and(|deadline| Instant::now() >= deadline)
                {
                    crate::debug_log::log(|| "recording time limit reached".into());
                    if TARGET_KEY.load(Ordering::Relaxed) == VK_RMENU
                        && RIGHT_ALT_STATE.load(Ordering::Relaxed) == 2
                    {
                        AUTO_STOPPED_ALT_HELD.store(true, Ordering::Relaxed);
                    }
                    stop(hwnd, true);
                    return 0;
                }
                let expires = app(hwnd).overlay_expires;
                if expires.is_some_and(|when| Instant::now() >= when) {
                    hide_overlay(hwnd);
                } else {
                    let level = app(hwnd).recording.as_ref().map(Recording::level);
                    overlay::tick(app(hwnd).overlay, level);
                    update_recording_elapsed(hwnd);
                }
            } else if wparam == RIGHT_ALT_TIMER {
                KillTimer(hwnd, RIGHT_ALT_TIMER);
                if RIGHT_ALT_STATE
                    .compare_exchange(1, 2, Ordering::Relaxed, Ordering::Relaxed)
                    .is_ok()
                {
                    crate::debug_log::log(|| "right_alt hold threshold: queue down".into());
                    PostMessageW(hwnd, KEY_MESSAGE, VK_RMENU as usize, 1);
                }
            } else if wparam == AUTO_SEND_DOWN_TIMER {
                KillTimer(hwnd, AUTO_SEND_DOWN_TIMER);
                send_keys(&[(VK_RETURN, false)]);
                if SetTimer(hwnd, AUTO_SEND_UP_TIMER, 40, None) == 0 {
                    send_keys(&[(VK_RETURN, true)]);
                }
            } else if wparam == AUTO_SEND_UP_TIMER {
                KillTimer(hwnd, AUTO_SEND_UP_TIMER);
                send_keys(&[(VK_RETURN, true)]);
            }
            0
        }
        WM_DESTROY => {
            crate::debug_log::log(|| "app shutdown: restoring audio".into());
            let owned = Box::from_raw(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut App);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            ROOT.store(0, Ordering::SeqCst);
            Shell_NotifyIconW(NIM_DELETE, &owned.tray);
            if owned.owned_icon {
                DestroyIcon(owned.tray.hIcon);
            }
            if !owned.hook.is_null() {
                UnhookWindowsHookEx(owned.hook);
            }
            if let Some(cancelled) = &owned.transcription_cancelled {
                cancelled.store(true, Ordering::Relaxed);
            }
            mute::restore(owned.mute_before);
            KillTimer(hwnd, DEV_STOP_TIMER);
            let event = DEV_STOP_EVENT.swap(0, Ordering::SeqCst) as HANDLE;
            if !event.is_null() {
                CloseHandle(event);
            }
            KillTimer(hwnd, 1);
            KillTimer(hwnd, RIGHT_ALT_TIMER);
            KillTimer(hwnd, AUTO_SEND_DOWN_TIMER);
            if KillTimer(hwnd, AUTO_SEND_UP_TIMER) != 0 {
                send_keys(&[(VK_RETURN, true)]);
            }
            if RIGHT_ALT_STATE.swap(0, Ordering::Relaxed) == 3 {
                send_keys(&[(VK_RMENU, true)]);
            }
            overlay::destroy(owned.overlay);
            if !owned.dialog.is_null() {
                DestroyWindow(owned.dialog);
            }
            PostQuitMessage(0);
            0
        }
        _ => DefWindowProcW(hwnd, message, wparam, lparam),
    }
}

unsafe fn tray_menu(hwnd: HWND) {
    let menu = CreatePopupMenu();
    if menu.is_null() {
        return;
    }
    AppendMenuW(
        menu,
        MF_STRING | MF_GRAYED,
        0,
        wide(&format!("상태: {}", app(hwnd).status)).as_ptr(),
    );
    AppendMenuW(
        menu,
        MF_STRING | MF_GRAYED,
        0,
        wide(&format!("최근 변환: {}", app(hwnd).last_text)).as_ptr(),
    );
    AppendMenuW(menu, MF_SEPARATOR, 0, ptr::null());
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, wide("설정...").as_ptr());
    AppendMenuW(menu, MF_STRING, ID_FOLDER, wide("설정 폴더 열기").as_ptr());
    AppendMenuW(menu, MF_SEPARATOR, 0, ptr::null());
    AppendMenuW(
        menu,
        MF_STRING | MF_GRAYED,
        0,
        wide(&format!(
            "버전 {}",
            option_env!("KEYSCRIBE_VERSION").unwrap_or(env!("CARGO_PKG_VERSION"))
        ))
        .as_ptr(),
    );
    AppendMenuW(menu, MF_STRING, ID_RESTART, wide("재실행").as_ptr());
    AppendMenuW(menu, MF_STRING, ID_EXIT, wide("종료").as_ptr());
    let mut point: POINT = mem::zeroed();
    GetCursorPos(&mut point);
    SetForegroundWindow(hwnd);
    let selected = TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON | TPM_RETURNCMD,
        point.x,
        point.y,
        0,
        hwnd,
        ptr::null(),
    );
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if selected != 0 {
        tray_command(hwnd, selected as usize);
    }
}

unsafe fn tray_command(hwnd: HWND, command: usize) {
    match command {
        ID_SETTINGS => show_settings(hwnd),
        ID_FOLDER => {
            let _ = std::process::Command::new("explorer.exe")
                .arg(settings::directory())
                .spawn();
        }
        ID_RESTART => {
            match std::env::current_exe().and_then(|exe| std::process::Command::new(exe).spawn()) {
                Ok(_) => {
                    DestroyWindow(hwnd);
                }
                Err(error) => set_status(hwnd, &format!("재실행 실패: {error}")),
            }
        }
        ID_EXIT => {
            DestroyWindow(hwnd);
        }
        _ => {}
    }
}

unsafe fn write_wide<const N: usize>(buffer: &mut [u16; N], text: &str) {
    buffer.fill(0);
    for (slot, character) in buffer.iter_mut().take(N - 1).zip(text.encode_utf16()) {
        *slot = character;
    }
}

unsafe fn set_status(hwnd: HWND, status: &str) {
    let state = app(hwnd);
    state.status = status.into();
    write_wide(&mut state.tray.szTip, &format!("KeyScribe · {status}"));
    Shell_NotifyIconW(NIM_MODIFY, &state.tray);
}

unsafe fn active_overlay(hwnd: HWND, state: overlay::State) {
    app(hwnd).overlay_expires = None;
    overlay::show(app(hwnd).overlay, state);
    SetTimer(hwnd, 1, 50, None);
}

unsafe fn update_recording_elapsed(hwnd: HWND) {
    let Some(started) = app(hwnd).recording_started_at else {
        return;
    };
    let seconds = started.elapsed().as_secs();
    if app(hwnd).recording_seconds_shown == Some(seconds) {
        return;
    }
    app(hwnd).recording_seconds_shown = Some(seconds);
    set_status(
        hwnd,
        &format!(
            "녹음 중 ({:02}:{:02}) · Esc 취소",
            seconds / 60,
            seconds % 60
        ),
    );
    let warning = app(hwnd).recording_deadline.is_some_and(|deadline| {
        deadline.saturating_duration_since(Instant::now()) <= Duration::from_secs(60)
    });
    overlay::update_recording_time(app(hwnd).overlay, seconds, warning);
}

unsafe fn transient_overlay(hwnd: HWND, state: overlay::State) {
    app(hwnd).overlay_expires = Some(Instant::now() + Duration::from_millis(2500));
    overlay::show(app(hwnd).overlay, state);
    SetTimer(hwnd, 1, 50, None);
}

unsafe fn hide_overlay(hwnd: HWND) {
    KillTimer(hwnd, 1);
    app(hwnd).overlay_expires = None;
    overlay::hide(app(hwnd).overlay);
}

fn shortcut_key(value: &str) -> u16 {
    if value == "right_option" {
        return VK_RMENU;
    }
    if value == "left_option" {
        return VK_LMENU;
    }
    SHORTCUTS
        .iter()
        .find(|item| item.0 == value)
        .map(|item| item.2)
        .unwrap_or(VK_RMENU)
}

unsafe fn handle_key(hwnd: HWND, key: u32, down: bool) {
    crate::debug_log::log(|| {
        format!(
            "handle_key key={key} down={down} recording={} transcribing={} pressed={} mode={}",
            app(hwnd).recording.is_some(),
            app(hwnd).transcribing,
            app(hwnd).pressed,
            app(hwnd).settings.recording_control
        )
    });
    if key == VK_ESCAPE as u32 && down {
        if app(hwnd).recording.is_some() || app(hwnd).transcribing {
            cancel(hwnd);
        }
        return;
    }
    if !is_recording_key(key, TARGET_KEY.load(Ordering::Relaxed) as u32) {
        return;
    }
    if down {
        if app(hwnd).pressed {
            return;
        }
        app(hwnd).pressed = true;
        if app(hwnd).recording.is_none() && !app(hwnd).transcribing {
            start(hwnd);
        } else if app(hwnd).recording.is_some() && app(hwnd).settings.recording_control == "toggle"
        {
            stop(hwnd, false);
        }
    } else {
        if !app(hwnd).pressed {
            return;
        }
        app(hwnd).pressed = false;
        if app(hwnd).recording.is_some() && app(hwnd).settings.recording_control == "hold" {
            stop(hwnd, false);
        }
    }
}

unsafe fn start(hwnd: HWND) {
    crate::debug_log::log(|| "recording start requested".into());
    if app(hwnd).limit_sound_playing.load(Ordering::SeqCst) {
        set_status(hwnd, "종료음 재생 중");
        return;
    }
    if app(hwnd).settings.api_key.is_empty() {
        show_settings(hwnd);
        return;
    }
    match Recording::start() {
        Ok(recording) => {
            app(hwnd).generation += 1;
            app(hwnd).recording = Some(recording);
            app(hwnd).recording_started_at = Some(Instant::now());
            app(hwnd).recording_deadline = Some(
                Instant::now()
                    + Duration::from_secs(
                        u64::from(app(hwnd).settings.recording_time_limit_minutes) * 60,
                    ),
            );
            app(hwnd).recording_seconds_shown = None;
            let volume = app(hwnd).settings.recording_start_sound_volume;
            active_overlay(hwnd, overlay::State::Recording);
            update_recording_elapsed(hwnd);
            if volume > 0 {
                let root = hwnd as isize;
                let generation = app(hwnd).generation;
                std::thread::spawn(move || {
                    let sound = recording_start_sound(volume);
                    unsafe {
                        PlaySoundW(
                            sound.as_ptr().cast(),
                            ptr::null_mut(),
                            SND_MEMORY | SND_NODEFAULT | SND_SYNC,
                        );
                        PostMessageW(root as HWND, CHIME_FINISHED_MESSAGE, generation as usize, 0);
                    }
                });
            } else if app(hwnd).settings.mute_during_recording {
                app(hwnd).mute_before = mute::mute();
                crate::debug_log::log(|| {
                    format!("recording mute result={:?}", app(hwnd).mute_before)
                });
            }
        }
        Err(error) => {
            crate::debug_log::log(|| format!("recording start failed: {error}"));
            set_status(hwnd, &format!("마이크 오류: {error}"));
            app(hwnd).overlay_expires = Some(Instant::now() + Duration::from_secs(5));
            let message = if error == "마이크를 찾을 수 없습니다" {
                "입력 마이크 없음"
            } else {
                "마이크 연결/설정 확인"
            };
            overlay::show_message(app(hwnd).overlay, message);
            SetTimer(hwnd, 1, 50, None);
        }
    }
}

fn recording_start_sound(volume: u16) -> Vec<u8> {
    let mut sound = include_bytes!("../../../assets/recording-start.wav").to_vec();
    if volume != 100 {
        let gain = f64::from(volume.min(200)) / 100.0;
        for sample in sound[44..].chunks_exact_mut(2) {
            let original = i16::from_le_bytes([sample[0], sample[1]]);
            let scaled = f64::from(original) / 32768.0 * gain;
            let magnitude = scaled.abs();
            let limited = if magnitude <= 0.8 {
                magnitude
            } else {
                0.8 + 0.2 * (1.0 - (-(magnitude - 0.8) / 0.2).exp())
            };
            let adjusted =
                ((if scaled < 0.0 { -limited } else { limited }) * 32767.0).round() as i16;
            sample.copy_from_slice(&adjusted.to_le_bytes());
        }
    }
    sound
}

unsafe fn play_recording_limit_sound(hwnd: HWND) {
    let playing = app(hwnd).limit_sound_playing.clone();
    playing.store(true, Ordering::SeqCst);
    std::thread::spawn(move || {
        let sound = include_bytes!("../../../assets/recording-limit.wav");
        unsafe {
            PlaySoundW(
                sound.as_ptr().cast(),
                ptr::null_mut(),
                SND_MEMORY | SND_NODEFAULT | SND_SYNC,
            );
        }
        playing.store(false, Ordering::SeqCst);
    });
}

unsafe fn stop(hwnd: HWND, limit_reached: bool) {
    crate::debug_log::log(|| "recording stop requested".into());
    let Some(recording) = app(hwnd).recording.take() else {
        return;
    };
    app(hwnd).recording_started_at = None;
    app(hwnd).recording_deadline = None;
    app(hwnd).recording_seconds_shown = None;
    let stopped = recording.stop();
    mute::restore(app(hwnd).mute_before.take());
    if limit_reached {
        play_recording_limit_sound(hwnd);
    }
    crate::debug_log::log(|| {
        format!(
            "recording stopped; audio restore attempted; success={}",
            stopped.is_ok()
        )
    });
    let wav = match stopped {
        Ok(path) => path,
        Err(error) => {
            set_status(hwnd, &format!("녹음 오류: {error}"));
            return;
        }
    };
    if std::fs::metadata(&wav)
        .map(|meta| meta.len() <= 44)
        .unwrap_or(true)
    {
        let _ = std::fs::remove_file(&wav);
        set_status(hwnd, "녹음된 음성이 없습니다");
        return;
    }
    app(hwnd).transcribing = true;
    let cancelled = Arc::new(AtomicBool::new(false));
    app(hwnd).transcription_cancelled = Some(cancelled.clone());
    app(hwnd).generation += 1;
    let generation = app(hwnd).generation;
    let settings = app(hwnd).settings.clone();
    set_status(
        hwnd,
        if limit_reached {
            "시간 제한 도달 · 변환 중"
        } else {
            "변환 중 · Esc 취소"
        },
    );
    active_overlay(hwnd, overlay::State::Transcribing);
    let root = hwnd as isize;
    std::thread::spawn(move || {
        let result = transcriber::transcribe(&wav, &settings, &cancelled);
        let _ = std::fs::remove_file(&wav);
        let message = Box::into_raw(Box::new(ResultMessage { generation, result }));
        unsafe {
            if PostMessageW(root as HWND, RESULT_MESSAGE, 0, message as isize) == 0 {
                drop(Box::from_raw(message));
            }
        }
    });
}

unsafe fn cancel(hwnd: HWND) {
    crate::debug_log::log(|| "recording cancelled; restoring audio".into());
    app(hwnd).generation += 1;
    if let Some(cancelled) = app(hwnd).transcription_cancelled.take() {
        cancelled.store(true, Ordering::Relaxed);
    }
    app(hwnd).recording = None;
    app(hwnd).recording_started_at = None;
    app(hwnd).recording_deadline = None;
    app(hwnd).recording_seconds_shown = None;
    mute::restore(app(hwnd).mute_before.take());
    app(hwnd).transcribing = false;
    set_status(hwnd, "취소됨");
    transient_overlay(hwnd, overlay::State::Cancelled);
}

unsafe fn paste(hwnd: HWND, text: &str, auto_send: bool) -> Result<(), String> {
    KillTimer(hwnd, AUTO_SEND_DOWN_TIMER);
    if OpenClipboard(ptr::null_mut()) == 0 {
        return Err("클립보드를 열 수 없습니다".into());
    }
    let mut owned = false;
    let outcome = (|| {
        if EmptyClipboard() == 0 {
            return Err("클립보드를 비울 수 없습니다".to_string());
        }
        let chars = wide(text);
        let bytes = chars.len() * mem::size_of::<u16>();
        let memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if memory.is_null() {
            return Err("클립보드 메모리가 부족합니다".into());
        }
        let destination = GlobalLock(memory) as *mut u16;
        if destination.is_null() {
            GlobalFree(memory);
            return Err("클립보드 메모리를 열 수 없습니다".into());
        }
        ptr::copy_nonoverlapping(chars.as_ptr(), destination, chars.len());
        GlobalUnlock(memory);
        if SetClipboardData(13, memory).is_null() {
            GlobalFree(memory);
            return Err("클립보드에 텍스트를 넣을 수 없습니다".into());
        }
        owned = true;
        Ok(())
    })();
    CloseClipboard();
    outcome?;
    if owned {
        send_keys(&[
            (VK_CONTROL, false),
            (b'V' as u16, false),
            (b'V' as u16, true),
            (VK_CONTROL, true),
        ]);
        if auto_send {
            // Editors can apply pasted text asynchronously. Keep the UI responsive
            // while waiting, then hold Return briefly like a physical key press.
            if SetTimer(hwnd, AUTO_SEND_DOWN_TIMER, 400, None) == 0 {
                return Err("Enter 입력을 예약할 수 없습니다".into());
            }
        }
    }
    Ok(())
}

unsafe fn send_keys(keys: &[(u16, bool)]) {
    let inputs: Vec<INPUT> = keys
        .iter()
        .map(|(key, up)| INPUT {
            r#type: INPUT_KEYBOARD,
            Anonymous: INPUT_0 {
                ki: KEYBDINPUT {
                    wVk: *key,
                    wScan: 0,
                    dwFlags: if *up { KEYEVENTF_KEYUP } else { 0 },
                    time: 0,
                    dwExtraInfo: 0,
                },
            },
        })
        .collect();
    SendInput(
        inputs.len() as u32,
        inputs.as_ptr(),
        mem::size_of::<INPUT>() as i32,
    );
}

unsafe fn info(owner: HWND, message: &str) {
    MessageBoxW(
        owner,
        wide(message).as_ptr(),
        wide("KeyScribe").as_ptr(),
        MB_OK | MB_ICONINFORMATION,
    );
}

unsafe fn open_api_key_page(owner: HWND, url: &str) {
    let result = ShellExecuteW(
        owner,
        wide("open").as_ptr(),
        wide(url).as_ptr(),
        ptr::null(),
        ptr::null(),
        SW_SHOWNORMAL,
    );
    if result as isize <= 32 {
        info(owner, "API 키 페이지를 열지 못했습니다.");
    }
}

unsafe fn open_log(owner: HWND) {
    let path = match crate::debug_log::ensure_file() {
        Ok(path) => path,
        Err(_) => {
            info(owner, "로그 파일을 만들 수 없습니다.");
            return;
        }
    };
    crate::debug_log::log(|| "log opened by user".into());
    let result = ShellExecuteW(
        owner,
        wide("open").as_ptr(),
        wide("notepad.exe").as_ptr(),
        wide(&path.to_string_lossy()).as_ptr(),
        ptr::null(),
        SW_SHOW,
    );
    if (result as isize) <= 32 {
        info(owner, "로그 파일을 열 수 없습니다.");
    }
}

unsafe fn show_settings(root: HWND) {
    let current = app(root).dialog;
    if !current.is_null() && IsWindow(current) != 0 {
        ShowWindow(current, SW_SHOW);
        SetForegroundWindow(current);
        return;
    }
    let instance = GetModuleHandleW(ptr::null());
    let dialog = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        wide("KeyScribeNativeSettings").as_ptr(),
        wide("KeyScribe 설정").as_ptr(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        560,
        700,
        root,
        ptr::null_mut(),
        instance,
        ptr::null(),
    );
    if dialog.is_null() {
        set_status(root, "설정 창을 열 수 없습니다");
        return;
    }
    let settings = app(root).settings.clone();
    let label = |text: &str, y: i32| {
        control(
            dialog,
            "STATIC",
            text,
            WS_CHILD | WS_VISIBLE,
            18,
            y,
            145,
            24,
            0,
        );
    };
    label("API 키", 20);
    let api_key = control(
        dialog,
        "EDIT",
        &settings.api_key,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD as u32 | ES_AUTOHSCROLL as u32,
        165,
        18,
        365,
        26,
        ID_API_KEY,
    );
    label("API 키 발급", 48);
    control(
        dialog,
        "BUTTON",
        "로그 보기",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON as u32,
        18,
        610,
        100,
        32,
        ID_LOG,
    );
    control(
        dialog,
        "BUTTON",
        "ElevenLabs 키 받기 ↗",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON as u32,
        165,
        47,
        175,
        20,
        ID_ELEVENLABS_KEY,
    );
    control(
        dialog,
        "BUTTON",
        "OpenAI 키 받기 ↗",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON as u32,
        350,
        47,
        180,
        20,
        ID_OPENAI_KEY,
    );
    label("전사 모델", 80);
    let model = control(
        dialog,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST as u32 | WS_VSCROLL,
        165,
        78,
        265,
        260,
        ID_MODEL,
    );
    let refresh = control(
        dialog,
        "BUTTON",
        "새로고침",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON as u32,
        440,
        78,
        90,
        28,
        ID_REFRESH,
    );
    let model_hint = control(
        dialog,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE,
        165,
        109,
        365,
        24,
        0,
    );
    label("인식 언어", 142);
    let language = control(
        dialog,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST as u32 | WS_VSCROLL,
        165,
        140,
        365,
        280,
        0,
    );
    let options = language_options(&settings.language);
    for (_, title) in &options {
        SendMessageW(language, CB_ADDSTRING, 0, wide(title).as_ptr() as isize);
    }
    let language_index = options
        .iter()
        .position(|(code, _)| code == &settings.language)
        .unwrap_or(0);
    SendMessageW(language, CB_SETCURSEL, language_index, 0);
    let language_codes = options.into_iter().map(|(code, _)| code).collect();
    label("녹음 단축키", 184);
    let shortcut = control(
        dialog,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST as u32 | WS_VSCROLL,
        165,
        182,
        365,
        190,
        0,
    );
    for (_, title, _) in SHORTCUTS {
        SendMessageW(shortcut, CB_ADDSTRING, 0, wide(title).as_ptr() as isize);
    }
    let selected = SHORTCUTS
        .iter()
        .position(|(code, _, _)| shortcut_key(code) == shortcut_key(&settings.shortcut))
        .unwrap_or(0);
    SendMessageW(shortcut, CB_SETCURSEL, selected, 0);
    label("녹음 방식", 226);
    let mode = control(
        dialog,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST as u32 | WS_VSCROLL,
        165,
        224,
        365,
        100,
        0,
    );
    for title in ["누르는 동안 녹음", "한번 누르면 녹음시작, 다시 누르면 종료"]
    {
        SendMessageW(mode, CB_ADDSTRING, 0, wide(title).as_ptr() as isize);
    }
    SendMessageW(
        mode,
        CB_SETCURSEL,
        usize::from(settings.recording_control == "toggle"),
        0,
    );
    label("녹음 시간 제한", 268);
    let recording_time_limit = control(
        dialog,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST as u32 | WS_VSCROLL,
        165,
        266,
        365,
        120,
        0,
    );
    for minutes in [10, 20, 30, 60] {
        SendMessageW(
            recording_time_limit,
            CB_ADDSTRING,
            0,
            wide(&format!("{minutes}분")).as_ptr() as isize,
        );
    }
    let selected_limit = [10, 20, 30, 60]
        .iter()
        .position(|minutes| *minutes == settings.recording_time_limit_minutes)
        .unwrap_or(2);
    SendMessageW(recording_time_limit, CB_SETCURSEL, selected_limit, 0);
    label("고유명사 (한 줄에 하나)", 308);
    let keyterms = control(
        dialog,
        "EDIT",
        &settings.keyterms.join("\r\n"),
        WS_CHILD
            | WS_VISIBLE
            | WS_BORDER
            | WS_VSCROLL
            | ES_MULTILINE as u32
            | ES_AUTOVSCROLL as u32,
        165,
        306,
        365,
        100,
        0,
    );
    let no_verbatim = control(
        dialog,
        "BUTTON",
        "군더더기 말 제거 (ElevenLabs)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX as u32,
        165,
        426,
        365,
        25,
        0,
    );
    SendMessageW(
        no_verbatim,
        BM_SETCHECK,
        usize::from(settings.no_verbatim),
        0,
    );
    let mute = control(
        dialog,
        "BUTTON",
        "녹음 중 시스템 소리 음소거",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX as u32,
        165,
        461,
        365,
        25,
        0,
    );
    SendMessageW(
        mute,
        BM_SETCHECK,
        usize::from(settings.mute_during_recording),
        0,
    );
    let auto_send = control(
        dialog,
        "BUTTON",
        "붙여넣은 뒤 Enter 입력",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX as u32,
        165,
        496,
        365,
        25,
        0,
    );
    SendMessageW(auto_send, BM_SETCHECK, usize::from(settings.auto_send), 0);
    label("녹음 시작 효과음", 531);
    let recording_start_sound_volume = control(
        dialog,
        "msctls_trackbar32",
        "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS,
        165,
        526,
        300,
        36,
        0,
    );
    SendMessageW(recording_start_sound_volume, TBM_SETRANGEMIN, 0, 0);
    SendMessageW(recording_start_sound_volume, TBM_SETRANGEMAX, 0, 200);
    SendMessageW(
        recording_start_sound_volume,
        TBM_SETPOS,
        1,
        settings.recording_start_sound_volume as isize,
    );
    let recording_start_sound_value = control(
        dialog,
        "STATIC",
        &format!("{}%", settings.recording_start_sound_volume),
        WS_CHILD | WS_VISIBLE,
        475,
        531,
        55,
        25,
        0,
    );
    control(
        dialog,
        "STATIC",
        "API 키는 이 컴퓨터의 사용자 설정에 저장됩니다.",
        WS_CHILD | WS_VISIBLE,
        165,
        570,
        365,
        26,
        0,
    );
    control(
        dialog,
        "BUTTON",
        "저장",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON as u32,
        348,
        610,
        85,
        32,
        ID_SAVE,
    );
    control(
        dialog,
        "BUTTON",
        "취소",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON as u32,
        445,
        610,
        85,
        32,
        ID_CANCEL,
    );
    let state = Box::new(Dialog {
        root,
        api_key,
        model,
        model_hint,
        refresh,
        shown_provider: None,
        openai_model: settings.openai_model,
        elevenlabs_model: settings.elevenlabs_model,
        request_id: 0,
        pending_key: None,
        language,
        language_codes,
        shortcut,
        mode,
        recording_time_limit,
        keyterms,
        no_verbatim,
        mute,
        recording_start_sound_volume,
        recording_start_sound_value,
        auto_send,
    });
    SetWindowLongPtrW(dialog, GWLP_USERDATA, Box::into_raw(state) as isize);
    app(root).dialog = dialog;
    update_provider(dialog);
    cached_or_refresh_models(dialog);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
}

unsafe fn control(
    parent: HWND,
    class: &str,
    title: &str,
    style: u32,
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    id: usize,
) -> HWND {
    let hwnd = CreateWindowExW(
        0,
        wide(class).as_ptr(),
        wide(title).as_ptr(),
        style,
        x,
        y,
        width,
        height,
        parent,
        id as HMENU,
        GetModuleHandleW(ptr::null()),
        ptr::null(),
    );
    if !hwnd.is_null() {
        let font = GetStockObject(DEFAULT_GUI_FONT);
        SendMessageW(hwnd, WM_SETFONT, font as usize, 1);
    }
    hwnd
}

unsafe fn dialog_state(hwnd: HWND) -> &'static mut Dialog {
    &mut *(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut Dialog)
}

unsafe fn text(hwnd: HWND) -> String {
    let length = GetWindowTextLengthW(hwnd);
    let mut buffer = vec![0u16; length as usize + 1];
    let actual = GetWindowTextW(hwnd, buffer.as_mut_ptr(), buffer.len() as i32);
    String::from_utf16_lossy(&buffer[..actual as usize])
}

fn provider(key: &str) -> Option<bool> {
    if key.starts_with("sk-") {
        Some(true)
    } else if key.starts_with("sk_") {
        Some(false)
    } else {
        None
    }
}

unsafe fn selected_model(hwnd: HWND) -> String {
    let index = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    if index < 0 {
        return String::new();
    }
    let length = SendMessageW(hwnd, CB_GETLBTEXTLEN, index as usize, 0);
    if length < 0 {
        return String::new();
    }
    let mut buffer = vec![0u16; length as usize + 1];
    SendMessageW(
        hwnd,
        CB_GETLBTEXT,
        index as usize,
        buffer.as_mut_ptr() as isize,
    );
    String::from_utf16_lossy(&buffer[..length as usize])
}

unsafe fn display_models(hwnd: HWND, models: &[String], preserve: bool) {
    let controls = dialog_state(hwnd);
    let previous = selected_model(controls.model);
    SendMessageW(controls.model, CB_RESETCONTENT, 0, 0);
    if preserve && !previous.is_empty() && !models.contains(&previous) {
        SendMessageW(
            controls.model,
            CB_ADDSTRING,
            0,
            wide(&previous).as_ptr() as isize,
        );
    }
    for name in models {
        SendMessageW(
            controls.model,
            CB_ADDSTRING,
            0,
            wide(name).as_ptr() as isize,
        );
    }
    let selected = if !previous.is_empty() && (preserve || models.contains(&previous)) {
        SendMessageW(
            controls.model,
            CB_FINDSTRINGEXACT,
            usize::MAX,
            wide(&previous).as_ptr() as isize,
        )
    } else {
        0
    };
    SendMessageW(controls.model, CB_SETCURSEL, selected.max(0) as usize, 0);
    SetWindowTextW(
        controls.model_hint,
        wide(&format!("전사 모델 {}개", models.len())).as_ptr(),
    );
    model_changed(hwnd);
}

unsafe fn cached_or_refresh_models(hwnd: HWND) {
    update_provider(hwnd);
    let controls = dialog_state(hwnd);
    let key = text(controls.api_key).trim().to_owned();
    if provider(&key).is_none() {
        SetWindowTextW(
            controls.model_hint,
            wide("OpenAI(sk-) 또는 ElevenLabs(sk_) API 키를 입력해 주세요.").as_ptr(),
        );
        return;
    }
    let cached = app(controls.root).model_cache.get(&key).cloned();
    if let Some(models) = cached {
        controls.pending_key = None;
        EnableWindow(controls.refresh, 1);
        display_models(hwnd, &models, true);
    } else if controls.pending_key.as_deref() != Some(key.as_str()) {
        refresh_models(hwnd, true);
    }
}

unsafe fn update_no_verbatim(dialog: HWND) {
    let state = dialog_state(dialog);
    let model = selected_model(state.model);
    let enabled = state.shown_provider == Some(false)
        && matches!(model.as_str(), "scribe_v2" | "scribe_v2_medical");
    EnableWindow(state.no_verbatim, i32::from(enabled));
}

unsafe fn model_changed(dialog: HWND) {
    let state = dialog_state(dialog);
    let selected = selected_model(state.model);
    if selected.is_empty() {
        update_no_verbatim(dialog);
        return;
    }
    match state.shown_provider {
        Some(true) => state.openai_model = selected,
        Some(false) => state.elevenlabs_model = selected,
        None => {}
    }
    update_no_verbatim(dialog);
}

unsafe fn update_provider(dialog: HWND) {
    let state = dialog_state(dialog);
    let next = provider(text(state.api_key).trim());
    if state.shown_provider != next {
        if state.shown_provider.is_some() {
            let selected = selected_model(state.model);
            if !selected.is_empty() {
                if state.shown_provider == Some(true) {
                    state.openai_model = selected;
                } else {
                    state.elevenlabs_model = selected;
                }
            }
        }
        state.shown_provider = next;
        SendMessageW(state.model, CB_RESETCONTENT, 0, 0);
        let saved = match next {
            Some(true) => &state.openai_model,
            Some(false) => &state.elevenlabs_model,
            None => "",
        };
        if !saved.is_empty() {
            SendMessageW(state.model, CB_ADDSTRING, 0, wide(saved).as_ptr() as isize);
            SendMessageW(state.model, CB_SETCURSEL, 0, 0);
        }
    }
    EnableWindow(state.model, i32::from(next.is_some()));
    EnableWindow(state.refresh, i32::from(next.is_some()));
    update_no_verbatim(dialog);
}

unsafe extern "system" fn dialog_proc(
    hwnd: HWND,
    message: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    if GetWindowLongPtrW(hwnd, GWLP_USERDATA) == 0 {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    match message {
        WM_HSCROLL => {
            let dialog = dialog_state(hwnd);
            if lparam as HWND == dialog.recording_start_sound_volume {
                let volume = SendMessageW(
                    dialog.recording_start_sound_volume,
                    TRACKBAR_GET_POSITION,
                    0,
                    0,
                )
                .clamp(0, 200);
                SetWindowTextW(
                    dialog.recording_start_sound_value,
                    wide(&format!("{volume}%")).as_ptr(),
                );
            }
            0
        }
        WM_COMMAND => {
            match loword(wparam) {
                ID_SAVE => save_dialog(hwnd),
                ID_CANCEL => {
                    DestroyWindow(hwnd);
                }
                ID_REFRESH => refresh_models(hwnd, false),
                ID_ELEVENLABS_KEY => {
                    open_api_key_page(hwnd, "https://elevenlabs.io/app/developers/api-keys")
                }
                ID_OPENAI_KEY => open_api_key_page(hwnd, "https://platform.openai.com/api-keys"),
                ID_LOG => open_log(hwnd),
                ID_API_KEY if (wparam >> 16) == EN_CHANGE as usize => {
                    dialog_state(hwnd).request_id = NEXT_REQUEST.fetch_add(1, Ordering::Relaxed);
                    dialog_state(hwnd).pending_key = None;
                    update_provider(hwnd);
                    SetWindowTextW(
                        dialog_state(hwnd).model_hint,
                        wide("API 키 변경 후 새로고침을 눌러 주세요.").as_ptr(),
                    );
                    KillTimer(hwnd, 2);
                    SetTimer(hwnd, 2, 500, None);
                }
                ID_API_KEY if (wparam >> 16) == EN_KILLFOCUS as usize => {
                    cached_or_refresh_models(hwnd)
                }
                ID_MODEL if (wparam >> 16) == CBN_SELCHANGE as usize => model_changed(hwnd),
                _ => {}
            }
            0
        }
        WM_TIMER if wparam == 2 => {
            KillTimer(hwnd, 2);
            cached_or_refresh_models(hwnd);
            0
        }
        WM_CLOSE => {
            DestroyWindow(hwnd);
            0
        }
        WM_DESTROY => {
            KillTimer(hwnd, 2);
            let dialog = Box::from_raw(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut Dialog);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if GetWindowLongPtrW(dialog.root, GWLP_USERDATA) != 0 {
                app(dialog.root).dialog = ptr::null_mut();
            }
            0
        }
        _ => DefWindowProcW(hwnd, message, wparam, lparam),
    }
}

unsafe fn save_dialog(hwnd: HWND) {
    model_changed(hwnd);
    let dialog = dialog_state(hwnd);
    let root = dialog.root;
    let mut updated = app(root).settings.clone();
    updated.api_key = text(dialog.api_key).trim().into();
    updated.openai_model = dialog.openai_model.clone();
    updated.elevenlabs_model = dialog.elevenlabs_model.clone();
    updated.language = dialog
        .language_codes
        .get(SendMessageW(dialog.language, CB_GETCURSEL, 0, 0) as usize)
        .cloned()
        .unwrap_or_else(|| updated.language.clone());
    updated.shortcut = SHORTCUTS
        .get(SendMessageW(dialog.shortcut, CB_GETCURSEL, 0, 0) as usize)
        .map(|entry| entry.0)
        .unwrap_or("right_alt")
        .into();
    updated.recording_control = if SendMessageW(dialog.mode, CB_GETCURSEL, 0, 0) == 1 {
        "toggle".into()
    } else {
        "hold".into()
    };
    updated.recording_time_limit_minutes = [10, 20, 30, 60]
        .get(SendMessageW(dialog.recording_time_limit, CB_GETCURSEL, 0, 0) as usize)
        .copied()
        .unwrap_or(30);
    updated.keyterms = text(dialog.keyterms)
        .lines()
        .map(str::trim)
        .filter(|term| !term.is_empty())
        .take(100)
        .map(str::to_owned)
        .collect();
    updated.no_verbatim = SendMessageW(dialog.no_verbatim, BM_GETCHECK, 0, 0) == 1;
    updated.mute_during_recording = SendMessageW(dialog.mute, BM_GETCHECK, 0, 0) == 1;
    updated.recording_start_sound_volume = SendMessageW(
        dialog.recording_start_sound_volume,
        TRACKBAR_GET_POSITION,
        0,
        0,
    )
    .clamp(0, 200) as u16;
    updated.auto_send = SendMessageW(dialog.auto_send, BM_GETCHECK, 0, 0) == 1;
    match updated.save() {
        Ok(()) => {
            TARGET_KEY.store(shortcut_key(&updated.shortcut), Ordering::SeqCst);
            app(root).settings = updated;
            app(root).pressed = false;
            set_status(root, "설정 저장됨");
            DestroyWindow(hwnd);
        }
        Err(error) => info(hwnd, &format!("설정을 저장하지 못했습니다: {error}")),
    }
}

unsafe fn refresh_models(hwnd: HWND, preserve: bool) {
    update_provider(hwnd);
    let dialog = dialog_state(hwnd);
    let mut settings = app(dialog.root).settings.clone();
    settings.api_key = text(dialog.api_key).trim().into();
    if provider(&settings.api_key).is_none() {
        SetWindowTextW(
            dialog.model_hint,
            wide("OpenAI(sk-) 또는 ElevenLabs(sk_) API 키를 입력해 주세요.").as_ptr(),
        );
        return;
    }
    dialog.request_id = NEXT_REQUEST.fetch_add(1, Ordering::Relaxed);
    let request_id = dialog.request_id;
    let key = settings.api_key.clone();
    dialog.pending_key = Some(key.clone());
    SetWindowTextW(
        dialog.model_hint,
        wide("사용 가능한 전사 모델을 불러오는 중…").as_ptr(),
    );
    EnableWindow(dialog.refresh, 0);
    let root = dialog.root as isize;
    std::thread::spawn(move || {
        let result = transcriber::models(&settings);
        let message = Box::into_raw(Box::new(ModelsMessage {
            result,
            request_id,
            key,
            preserve,
        }));
        unsafe {
            if PostMessageW(root as HWND, MODELS_MESSAGE, 0, message as isize) == 0 {
                drop(Box::from_raw(message));
            }
        }
    });
}
