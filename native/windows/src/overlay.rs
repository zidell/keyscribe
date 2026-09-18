use std::{mem, ptr};
use windows_sys::Win32::{
    Foundation::HWND,
    Graphics::Gdi::{
        BeginPaint, CreateFontW, CreateSolidBrush, DeleteObject, Ellipse, EndPaint, FillRect, GetMonitorInfoW,
        GetStockObject, GetTextExtentPoint32W, InvalidateRect, MonitorFromPoint, RoundRect,
        SelectObject, SetBkMode, SetTextColor, TextOutW, DEFAULT_GUI_FONT, MONITORINFO,
        MONITOR_DEFAULTTONEAREST, NULL_PEN, PAINTSTRUCT,
    },
    UI::WindowsAndMessaging::{
        CreateWindowExW, DefWindowProcW, DestroyWindow, GetCursorPos, GetWindowLongPtrW,
        LoadCursorW, RegisterClassW, SetLayeredWindowAttributes, SetWindowLongPtrW, SetWindowPos,
        ShowWindow, GWLP_USERDATA, HWND_TOPMOST, IDC_ARROW, LWA_ALPHA, LWA_COLORKEY,
        SWP_NOACTIVATE, SW_HIDE, SW_SHOWNOACTIVATE, WM_DESTROY, WM_PAINT, WNDCLASSW, WS_EX_LAYERED,
        WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TRANSPARENT, WS_POPUP,
    },
};

const WIDTH: i32 = 260;
const HEIGHT: i32 = 52;

pub enum State {
    Recording,
    Transcribing,
    Cancelled,
    Failed,
}

impl State {
    fn title(&self) -> &'static str {
        match self {
            Self::Recording => "녹음 중",
            Self::Transcribing => "변환 중...",
            Self::Cancelled => "녹음 취소됨",
            Self::Failed => "변환 실패",
        }
    }
}

struct Data {
    title: String,
    indicator: Indicator,
    time_warning: bool,
    level: f32,
    phase: f32,
}

#[derive(Clone, Copy, PartialEq)]
enum Indicator {
    None,
    Recording,
    Transcribing,
}

fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(Some(0)).collect()
}

pub unsafe fn register(instance: *mut std::ffi::c_void) -> bool {
    let class = wide("KeyScribeNativeOverlay");
    let definition = WNDCLASSW {
        style: 0,
        lpfnWndProc: Some(procedure),
        cbClsExtra: 0,
        cbWndExtra: 0,
        hInstance: instance,
        hIcon: ptr::null_mut(),
        hCursor: LoadCursorW(ptr::null_mut(), IDC_ARROW),
        hbrBackground: ptr::null_mut(),
        lpszMenuName: ptr::null(),
        lpszClassName: class.as_ptr(),
    };
    RegisterClassW(&definition) != 0
}

pub unsafe fn create(instance: *mut std::ffi::c_void, owner: HWND) -> HWND {
    let hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        wide("KeyScribeNativeOverlay").as_ptr(),
        wide("").as_ptr(),
        WS_POPUP,
        0,
        0,
        WIDTH,
        HEIGHT,
        owner,
        ptr::null_mut(),
        instance,
        ptr::null(),
    );
    if !hwnd.is_null() {
        let data = Box::new(Data {
            title: String::new(),
            indicator: Indicator::None,
            time_warning: false,
            level: 0.0,
            phase: 0.0,
        });
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, Box::into_raw(data) as isize);
        SetLayeredWindowAttributes(hwnd, 0x00ff00ff, 235, LWA_ALPHA | LWA_COLORKEY);
    }
    hwnd
}

pub unsafe fn show(hwnd: HWND, state: State) {
    let indicator = match state {
        State::Recording => Indicator::Recording,
        State::Transcribing => Indicator::Transcribing,
        State::Cancelled | State::Failed => Indicator::None,
    };
    show_with_message(hwnd, state.title(), indicator);
}

pub unsafe fn show_message(hwnd: HWND, message: &str) {
    show_with_message(hwnd, message, Indicator::None);
}

pub unsafe fn update_recording_time(hwnd: HWND, seconds: u64, warning: bool) {
    if hwnd.is_null() {
        return;
    }
    let data = &mut *(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut Data);
    if data.indicator == Indicator::Recording {
        data.title = format!("녹음 중 ({:02}:{:02})", seconds / 60, seconds % 60);
        data.time_warning = warning;
        InvalidateRect(hwnd, ptr::null(), 0);
    }
}

unsafe fn show_with_message(hwnd: HWND, message: &str, indicator: Indicator) {
    if hwnd.is_null() {
        return;
    }
    let data = &mut *(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut Data);
    data.title = message.into();
    data.indicator = indicator;
    data.time_warning = false;
    data.level = 0.0;
    data.phase = 0.0;
    let mut pointer = mem::zeroed();
    GetCursorPos(&mut pointer);
    let monitor = MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST);
    let mut info = MONITORINFO {
        cbSize: mem::size_of::<MONITORINFO>() as u32,
        rcMonitor: mem::zeroed(),
        rcWork: mem::zeroed(),
        dwFlags: 0,
    };
    if GetMonitorInfoW(monitor, &mut info) != 0 {
        let x = (info.rcWork.left + info.rcWork.right - WIDTH) / 2;
        let y = info.rcWork.bottom - HEIGHT - 40;
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, WIDTH, HEIGHT, SWP_NOACTIVATE);
    }
    InvalidateRect(hwnd, ptr::null(), 1);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
}

pub unsafe fn tick(hwnd: HWND, level: Option<f32>) {
    if hwnd.is_null() {
        return;
    }
    let data = &mut *(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut Data);
    data.phase += 0.4;
    data.level = if let Some(level) = level {
        level.clamp(0.0, 1.0)
    } else {
        data.level * 0.88
    };
    InvalidateRect(hwnd, ptr::null(), 0);
}

pub unsafe fn hide(hwnd: HWND) {
    if !hwnd.is_null() {
        ShowWindow(hwnd, SW_HIDE);
    }
}

pub unsafe fn destroy(hwnd: HWND) {
    if !hwnd.is_null() {
        DestroyWindow(hwnd);
    }
}

unsafe extern "system" fn procedure(
    hwnd: HWND,
    message: u32,
    wparam: usize,
    lparam: isize,
) -> isize {
    match message {
        WM_PAINT => {
            let mut paint: PAINTSTRUCT = mem::zeroed();
            let dc = BeginPaint(hwnd, &mut paint);
            let rect = windows_sys::Win32::Foundation::RECT {
                left: 0,
                top: 0,
                right: WIDTH,
                bottom: HEIGHT,
            };
            let transparent = CreateSolidBrush(0x00ff00ff);
            FillRect(dc, &rect, transparent);
            DeleteObject(transparent as _);
            let background = CreateSolidBrush(0x001a1a1a);
            let old_brush = SelectObject(dc, background as _);
            let old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, 0, 0, WIDTH, HEIGHT, 52, 52);
            SelectObject(dc, old_brush);
            DeleteObject(background as _);
            SetBkMode(dc, 1);
            SetTextColor(dc, 0x00ffffff);
            let old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            if GetWindowLongPtrW(hwnd, GWLP_USERDATA) != 0 {
                let data = &*(GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *const Data);
                if data.indicator == Indicator::Recording {
                    if let Some(time) = data.title.strip_prefix("녹음 중 ") {
                        let prefix: Vec<u16> = "녹음 중 ".encode_utf16().collect();
                        let time: Vec<u16> = time.encode_utf16().collect();
                        TextOutW(dc, 40, 18, prefix.as_ptr(), prefix.len() as i32);
                        let mut prefix_size = mem::zeroed();
                        GetTextExtentPoint32W(
                            dc,
                            prefix.as_ptr(),
                            prefix.len() as i32,
                            &mut prefix_size,
                        );
                        SetTextColor(
                            dc,
                            if data.time_warning {
                                0x005959ff
                            } else {
                                0x008d8d8d
                            },
                        );
                        TextOutW(
                            dc,
                            40 + prefix_size.cx,
                            18,
                            time.as_ptr(),
                            time.len() as i32,
                        );
                    } else {
                        let title: Vec<u16> = data.title.encode_utf16().collect();
                        TextOutW(dc, 40, 18, title.as_ptr(), title.len() as i32);
                    }
                } else if data.indicator == Indicator::Transcribing {
                    let title: Vec<u16> = data.title.encode_utf16().collect();
                    TextOutW(dc, 40, 18, title.as_ptr(), title.len() as i32);
                } else {
                    let title: Vec<u16> = data.title.encode_utf16().collect();
                    TextOutW(dc, 20, 18, title.as_ptr(), title.len() as i32);
                }
                if data.indicator == Indicator::Recording {
                    let brush = CreateSolidBrush(0x004747ff);
                    SelectObject(dc, brush as _);
                    Ellipse(dc, 19, 20, 31, 32);
                    SelectObject(dc, old_brush);
                    DeleteObject(brush as _);
                } else if data.indicator == Indicator::Transcribing {
                    let frames = ["\u{280b}", "\u{2819}", "\u{2839}", "\u{2838}", "\u{283c}", "\u{2834}", "\u{2826}", "\u{2827}"];
                    let frame = frames[(data.phase as usize) % frames.len()];
                    let glyph: Vec<u16> = frame.encode_utf16().collect();
                    SetTextColor(dc, 0x00b0b0b0);
                    let spinner_font = CreateFontW(
                        -15, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0,
                        wide("Segoe UI Symbol").as_ptr(),
                    );
                    if !spinner_font.is_null() {
                        let previous_font = SelectObject(dc, spinner_font as _);
                        let mut glyph_size = mem::zeroed();
                        GetTextExtentPoint32W(
                            dc,
                            glyph.as_ptr(),
                            glyph.len() as i32,
                            &mut glyph_size,
                        );
                        TextOutW(
                            dc,
                            25 - glyph_size.cx / 2,
                            26 - glyph_size.cy / 2,
                            glyph.as_ptr(),
                            glyph.len() as i32,
                        );
                        SelectObject(dc, previous_font);
                        DeleteObject(spinner_font as _);
                    } else {
                        TextOutW(dc, 18, 17, glyph.as_ptr(), glyph.len() as i32);
                    }
                }
                for index in 0..5 {
                    let wave = (data.phase + index as f32 * 1.3).sin() * 0.5 + 0.5;
                    let strength =
                        data.level * (0.5 + 0.5 * wave) + (1.0 - data.level) * 0.12 * wave;
                    let height = (4.0 + strength * 24.0) as i32;
                    let x = 210 + index * 7;
                    RoundRect(
                        dc,
                        x,
                        (HEIGHT - height) / 2,
                        x + 3,
                        (HEIGHT + height) / 2,
                        3,
                        3,
                    );
                }
            }
            SelectObject(dc, old_pen);
            SelectObject(dc, old_font);
            EndPaint(hwnd, &paint);
            0
        }
        WM_DESTROY => {
            let pointer = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut Data;
            if !pointer.is_null() {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                drop(Box::from_raw(pointer));
            }
            0
        }
        _ => DefWindowProcW(hwnd, message, wparam, lparam),
    }
}
