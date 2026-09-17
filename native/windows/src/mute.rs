use std::time::Duration;
use windows::Win32::{
    Media::Audio::{
        eMultimedia, eRender, Endpoints::IAudioEndpointVolume, IMMDeviceEnumerator,
        MMDeviceEnumerator,
    },
    System::Com::{CoCreateInstance, CoInitializeEx, CLSCTX_ALL, COINIT_MULTITHREADED},
};

#[derive(Debug, Clone, Copy)]
pub struct MuteState {
    was_muted: bool,
    volume: Option<f32>,
}

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

pub fn mute() -> Option<MuteState> {
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
        let volume = match endpoint.GetMasterVolumeLevelScalar() {
            Ok(value) => Some(value),
            Err(error) => {
                crate::debug_log::log(|| format!("audio volume query failed; muting without fade: {error}"));
                None
            }
        };
        if !was_muted {
            if let Some(volume) = volume {
                for step in 1..=5 {
                    let level = volume * (5 - step) as f32 / 5.0;
                    if let Err(error) = endpoint.SetMasterVolumeLevelScalar(level, std::ptr::null()) {
                        crate::debug_log::log(|| format!("audio fade failed: {error}"));
                        let _ = endpoint.SetMasterVolumeLevelScalar(volume, std::ptr::null());
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(25));
                }
            }
            if let Err(error) = endpoint.SetMute(true, std::ptr::null()) {
                crate::debug_log::log(|| format!("audio mute failed: {error}"));
                if let Some(volume) = volume {
                    let _ = endpoint.SetMasterVolumeLevelScalar(volume, std::ptr::null());
                }
                return None;
            }
            if let Some(volume) = volume {
                let _ = endpoint.SetMasterVolumeLevelScalar(volume, std::ptr::null());
            }
        }
        crate::debug_log::log(|| format!("audio mute set previous={was_muted}"));
        Some(MuteState { was_muted, volume })
    }
}

pub fn restore(state: Option<MuteState>) {
    if let Some(state) = state.filter(|state| !state.was_muted) {
        for attempt in 1..=3 {
            let result = endpoint().and_then(|endpoint| unsafe {
                if let Some(volume) = state.volume {
                    let _ = endpoint.SetMasterVolumeLevelScalar(volume, std::ptr::null());
                }
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
                        std::thread::sleep(Duration::from_millis(200));
                    }
                }
            }
        }
    } else {
        crate::debug_log::log(|| format!("audio restore skipped previous={state:?}"));
    }
}
