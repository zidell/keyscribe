use windows::Win32::{
    Media::Audio::{
        eMultimedia, eRender, Endpoints::IAudioEndpointVolume, IMMDeviceEnumerator,
        MMDeviceEnumerator,
    },
    System::Com::{CoCreateInstance, CoInitializeEx, CLSCTX_ALL, COINIT_MULTITHREADED},
};

pub fn initialize() {
    unsafe {
        let _ = CoInitializeEx(None, COINIT_MULTITHREADED);
    }
}

fn endpoint() -> windows::core::Result<IAudioEndpointVolume> {
    unsafe {
        let enumerator: IMMDeviceEnumerator =
            CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)?;
        let device = enumerator.GetDefaultAudioEndpoint(eRender, eMultimedia)?;
        device.Activate(CLSCTX_ALL, None)
    }
}

pub fn mute() -> Option<bool> {
    let endpoint = endpoint().ok()?;
    unsafe {
        let was_muted = endpoint.GetMute().ok()?.as_bool();
        if !was_muted {
            endpoint.SetMute(true, std::ptr::null()).ok()?;
        }
        Some(was_muted)
    }
}

pub fn restore(was_muted: Option<bool>) {
    if was_muted == Some(false) {
        if let Ok(endpoint) = endpoint() {
            unsafe {
                let _ = endpoint.SetMute(false, std::ptr::null());
            }
        }
    }
}
