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

    /// 앱이 음소거한 사실을 파일로 남긴다. 녹음 중 앱이 강제로 끝나 되돌리지 못하면
    /// 다음 실행 때 이 표시를 보고 음소거를 풀어 준다.
    private static let leftoverMuteURL = Settings.directory.appendingPathComponent("muted-by-keyscribe.json")

    static func rememberMute(volume: Float32?) {
        var marker: [String: Any] = [:]
        if let volume { marker["volume"] = volume }
        guard let data = try? JSONSerialization.data(withJSONObject: marker) else { return }
        try? data.write(to: leftoverMuteURL, options: .atomic)
    }

    static func forgetMute() {
        try? FileManager.default.removeItem(at: leftoverMuteURL)
    }

    /// 지난 실행이 음소거를 풀지 못하고 끝났으면 기본 출력 장치의 음소거를 풀고 볼륨을 되돌린다.
    static func restoreLeftoverMute() -> Bool {
        guard let data = try? Data(contentsOf: leftoverMuteURL) else { return false }
        let marker = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] ?? [:]
        guard let output = capture() else { return false }
        if let volume = (marker["volume"] as? NSNumber)?.floatValue { setVolume(volume, on: output) }
        guard setMuted(false, on: output) else { return false }
        forgetMute()
        return true
    }

    @discardableResult
    static func setMuted(_ muted: Bool, on snapshot: Snapshot) -> Bool {
        var muteAddress = address(kAudioDevicePropertyMute)
        var value: UInt32 = muted ? 1 : 0
        return AudioObjectSetPropertyData(snapshot.device, &muteAddress, 0, nil,
                                          UInt32(MemoryLayout<UInt32>.size), &value) == noErr
    }
}
