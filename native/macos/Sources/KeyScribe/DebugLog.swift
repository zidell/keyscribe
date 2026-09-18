import Foundation

final class DebugLog {
    static let shared = DebugLog()

    private let queue = DispatchQueue(label: "net.gitools.keyscribe.debug-log", qos: .utility)
    let fileURL: URL
    private var timer: DispatchSourceTimer?
    private let retention: TimeInterval = 24 * 60 * 60

    private init() {
        if let path = ProcessInfo.processInfo.environment["KEYSCRIBE_DEBUG_LOG"], !path.isEmpty {
            fileURL = URL(fileURLWithPath: path)
        } else {
            fileURL = Settings.directory.appendingPathComponent("debug.log")
        }
        try? FileManager.default.createDirectory(at: fileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        if !FileManager.default.fileExists(atPath: fileURL.path) {
            FileManager.default.createFile(atPath: fileURL.path, contents: nil)
        }
        queue.async { self.prune() }
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + 60, repeating: 60)
        timer.setEventHandler { [weak self] in self?.prune() }
        timer.resume()
        self.timer = timer
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
}
