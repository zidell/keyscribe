import Foundation

final class DebugLog {
    static let shared = DebugLog()

    private let queue = DispatchQueue(label: "net.gitools.keyscribe.debug-log", qos: .utility)
    let fileURL: URL
    private var timer: DispatchSourceTimer?
    private var retention: TimeInterval = TimeInterval(Settings.defaultLogRetentionHours) * 60 * 60

    private init() {
        if let path = ProcessInfo.processInfo.environment["KEYSCRIBE_DEBUG_LOG"], !path.isEmpty {
            fileURL = URL(fileURLWithPath: path)
        } else {
            fileURL = Settings.directory.appendingPathComponent("logs/debug.log")
            // 예전에는 앱 데이터 폴더 바로 아래에 로그를 두었다.
            let legacy = Settings.directory.appendingPathComponent("debug.log")
            try? FileManager.default.createDirectory(at: fileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            if FileManager.default.fileExists(atPath: legacy.path) {
                if (try? FileManager.default.moveItem(at: legacy, to: fileURL)) == nil {
                    try? FileManager.default.removeItem(at: legacy)
                }
            }
        }
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if !FileManager.default.fileExists(atPath: fileURL.path) {
            FileManager.default.createFile(atPath: fileURL.path, contents: nil)
        }
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + 60, repeating: 60)
        timer.setEventHandler { [weak self] in self?.prune() }
        timer.resume()
        self.timer = timer
    }

    /// 로그와 녹음 원본을 함께 두는 폴더.
    var directory: URL { fileURL.deletingLastPathComponent() }

    /// 보존 기간을 바꾸고 바로 지난 기록을 지운다. 설정 값을 받기 전에는 지우지 않는다.
    func setRetention(hours: Int) {
        queue.async {
            self.retention = TimeInterval(hours) * 60 * 60
            self.prune()
        }
    }

    /// 전사가 실패해도 다시 쓸 수 있게 녹음 원본을 남길 경로. 로그와 같은 보존 기간이 지나면 지운다.
    func newRecordingURL() -> URL {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyyMMdd-HHmmss-SSS"
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        return directory.appendingPathComponent("recording-\(formatter.string(from: Date())).wav")
    }

    func record(_ message: String) {
        queue.async {
            let line = "\(Int64(Date().timeIntervalSince1970 * 1000)) \(message)\n"
            guard let data = line.data(using: .utf8) else { return }
            if !FileManager.default.fileExists(atPath: self.fileURL.path) {
                FileManager.default.createFile(atPath: self.fileURL.path, contents: nil)
            }
            guard let handle = try? FileHandle(forWritingTo: self.fileURL) else { return }
            defer { try? handle.close() }
            try? handle.seekToEnd()
            try? handle.write(contentsOf: data)
        }
    }

    private func prune() {
        pruneRecordings()
        guard let contents = try? String(contentsOf: fileURL, encoding: .utf8) else { return }
        let cutoff = Int64((Date().timeIntervalSince1970 - retention) * 1000)
        let lines = contents.split(separator: "\n").filter { line in
            guard let first = line.split(separator: " ", maxSplits: 1).first,
                  let timestamp = Int64(first) else { return false }
            return timestamp >= cutoff
        }
        let kept = lines.isEmpty ? "" : lines.joined(separator: "\n") + "\n"
        try? kept.write(to: fileURL, atomically: true, encoding: .utf8)
    }

    private func pruneRecordings() {
        let cutoff = Date().addingTimeInterval(-retention)
        let files = (try? FileManager.default.contentsOfDirectory(
            at: directory, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        for file in files where file.lastPathComponent.hasPrefix("recording-") && file.pathExtension == "wav" {
            guard let modified = try? file.resourceValues(forKeys: [.contentModificationDateKey])
                .contentModificationDate, modified < cutoff else { continue }
            try? FileManager.default.removeItem(at: file)
        }
    }
}
