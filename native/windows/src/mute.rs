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
    let endpoint = match endpoint() {
        Ok(endpoint) => endpoint,
        Err(error) => {
            crate::debug_log::log(|| format!("audio mute endpoint failed: {error}"));
            return None;
        }
    };
    unsafe {
        let was_muted = match endpoint.GetMute() {
            Ok(value) => value.as_bool(),
            Err(error) => {
                crate::debug_log::log(|| format!("audio mute state query failed: {error}"));
                return None;
            }
        };
        if !was_muted {
            if let Err(error) = endpoint.SetMute(true, std::ptr::null()) {
                crate::debug_log::log(|| format!("audio mute failed: {error}"));
                return None;
            }
        }
        crate::debug_log::log(|| format!("audio mute set previous={was_muted}"));
        Some(was_muted)
    }
}

pub fn restore(was_muted: Option<bool>) {
    if was_muted == Some(false) {
        for attempt in 1..=3 {
            let result = endpoint().and_then(|endpoint| unsafe {
                endpoint.SetMute(false, std::ptr::null())
            });
            match result {
                Ok(()) => {
                    crate::debug_log::log(|| format!("audio restored attempt={attempt}"));
                    return;
                }
                Err(error) => {
                    crate::debug_log::log(|| format!("audio restore failed attempt={attempt}: {error}"));
                    if attempt < 3 {
                        std::thread::sleep(std::time::Duration::from_millis(200));
                    }
                }
            }
        }
    } else {
        crate::debug_log::log(|| format!("audio restore skipped previous={was_muted:?}"));
    }
}
