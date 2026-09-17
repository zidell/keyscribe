import AudioToolbox
import CoreAudio

enum SystemAudioOutput {
    struct Snapshot {
        let device: AudioObjectID
        let volume: Float32?
        let wasMuted: Bool
    }

    private static func address(_ selector: AudioObjectPropertySelector) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector,
                                   mScope: kAudioObjectPropertyScopeOutput,
                                   mElement: kAudioObjectPropertyElementMain)
    }

    static func capture() -> Snapshot? {
        var defaultAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultOutputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
        var device = AudioObjectID(0)
        var deviceSize = UInt32(MemoryLayout<AudioObjectID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject),
                                         &defaultAddress, 0, nil, &deviceSize, &device) == noErr else { return nil }

        var muteAddress = address(kAudioDevicePropertyMute)
        var muted: UInt32 = 0
        var muteSize = UInt32(MemoryLayout<UInt32>.size)
        guard AudioObjectGetPropertyData(device, &muteAddress, 0, nil, &muteSize, &muted) == noErr else { return nil }

        var volumeAddress = address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume)
        var volume: Float32 = 0
        var volumeSize = UInt32(MemoryLayout<Float32>.size)
        let hasVolume = AudioObjectGetPropertyData(device, &volumeAddress, 0, nil, &volumeSize, &volume) == noErr
        return Snapshot(device: device, volume: hasVolume ? volume : nil, wasMuted: muted != 0)
    }

    @discardableResult
    static func setVolume(_ volume: Float32, on snapshot: Snapshot) -> Bool {
        var volumeAddress = address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume)
        var value = max(0, min(1, volume))
        return AudioObjectSetPropertyData(snapshot.device, &volumeAddress, 0, nil,
                                          UInt32(MemoryLayout<Float32>.size), &value) == noErr
    }

    @discardableResult
    static func setMuted(_ muted: Bool, on snapshot: Snapshot) -> Bool {
        var muteAddress = address(kAudioDevicePropertyMute)
        var value: UInt32 = muted ? 1 : 0
        return AudioObjectSetPropertyData(snapshot.device, &muteAddress, 0, nil,
                                          UInt32(MemoryLayout<UInt32>.size), &value) == noErr
    }
}
