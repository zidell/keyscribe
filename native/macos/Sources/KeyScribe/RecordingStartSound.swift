import AVFoundation
import Foundation

/// 녹음 시작 효과음. 녹음 시작과 설정 화면의 미리 듣기가 같은 소리를 낸다.
enum RecordingStartSound {
    /// 100% 이하는 플레이어 볼륨으로 줄이고, 그보다 크면 샘플을 키우되 끝을 부드럽게 눌러 찢어지지 않게 한다.
    static func make(volume: Int) -> AVAudioPlayer? {
        guard let url = Bundle.main.url(forResource: "recording-start", withExtension: "wav") else { return nil }
        if volume <= 100 {
            guard let sound = try? AVAudioPlayer(contentsOf: url) else { return nil }
            sound.volume = Float(volume) / 100
            return sound
        }
        guard var data = try? Data(contentsOf: url), data.count >= 44 else { return nil }
        let gain = Double(volume) / 100
        data.withUnsafeMutableBytes { (bytes: UnsafeMutableRawBufferPointer) in
            for offset in stride(from: 44, to: bytes.count - 1, by: 2) {
                let sample = Int16(bitPattern: UInt16(bytes[offset]) | (UInt16(bytes[offset + 1]) << 8))
                let scaled = Double(sample) / 32768 * gain
                let magnitude = abs(scaled)
                let limited = magnitude <= 0.8 ? magnitude : 0.8 + 0.2 * (1 - exp(-(magnitude - 0.8) / 0.2))
                let adjusted = Int16(((scaled < 0 ? -limited : limited) * 32767).rounded())
                let encoded = UInt16(bitPattern: adjusted)
                bytes[offset] = UInt8(truncatingIfNeeded: encoded)
                bytes[offset + 1] = UInt8(truncatingIfNeeded: encoded >> 8)
            }
        }
        return try? AVAudioPlayer(data: data)
    }
}
