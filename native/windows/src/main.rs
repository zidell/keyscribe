#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod audio;
mod debug_log;
mod keys;
mod mute;
mod overlay;
mod settings;
mod transcriber;
mod ui;

fn main() {
    if let Err(error) = ui::run() {
        debug_log::log(|| format!("app startup/runtime failed: {error}"));
        eprintln!("KeyScribe: {error}");
        unsafe {
            use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_ICONERROR, MB_OK};
            let title: Vec<u16> = "KeyScribe\0".encode_utf16().collect();
            let message: Vec<u16> = format!("{error}\0").encode_utf16().collect();
            MessageBoxW(
                std::ptr::null_mut(),
                message.as_ptr(),
                title.as_ptr(),
                MB_OK | MB_ICONERROR,
            );
        }
    }
}
