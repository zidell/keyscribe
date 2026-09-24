import Foundation

struct Settings {
    var apiKey = ""
    var shortcut = "right_command"
    var recordingControl = "hold"
    var recordingTimeLimitMinutes = 30
    var logRetentionHours = Settings.defaultLogRetentionHours
    var autoSend = true
    var language = "ko"
    var keyterms: [String] = []
    var replacements: [String] = []
    var noVerbatim = true
    var muteDuringRecording = true
    var recordingStartSoundVolume = 100
    var overlayPosition = "bottom_center"
    var openAIModel = "gpt-transcribe"
    var elevenLabsModel = "scribe_v2"
    var groqModel = "whisper-large-v3-turbo"

    static let defaultLogRetentionHours = 168
    static let logRetentionOptions = [1, 24, 168, 720]

    static let directory = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/keyscribe", isDirectory: true)
    static let configURL = directory.appendingPathComponent("config.toml")
    static let userURL = directory.appendingPathComponent("user_config.json")
    static let legacyUserURL = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/voice-stt/user_config.json")

    static func load() -> Settings {
        var result = Settings()
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if !FileManager.default.fileExists(atPath: configURL.path),
           let example = Bundle.main.url(forResource: "config.toml", withExtension: "example"),
           let contents = try? String(contentsOf: example, encoding: .utf8) {
            let macDefaults = contents.replacingOccurrences(of: "shortcut = \"right_option\"",
                                                            with: "shortcut = \"right_command\"")
            try? macDefaults.write(to: configURL, atomically: true, encoding: .utf8)
        }
        if let contents = try? String(contentsOf: configURL, encoding: .utf8) {
            let values = parseTOML(contents)
            result.shortcut = values["shortcut"] as? String ?? result.shortcut
            if result.shortcut == "right_cmd" { result.shortcut = "right_command" }
            result.recordingControl = values["recording_control"] as? String ?? result.recordingControl
            if let limit = values["recording_time_limit_minutes"] as? Int,
               [10, 20, 30, 60].contains(limit) {
                result.recordingTimeLimitMinutes = limit
            }
            if let hours = values["log_retention_hours"] as? Int,
               logRetentionOptions.contains(hours) {
                result.logRetentionHours = hours
            }
            result.autoSend = values["auto_send"] as? Bool ?? result.autoSend
            result.language = values["language"] as? String ?? result.language
            result.keyterms = values["keyterms"] as? [String] ?? result.keyterms
            result.replacements = values["replacements"] as? [String] ?? result.replacements
            result.noVerbatim = values["no_verbatim"] as? Bool ?? result.noVerbatim
            result.muteDuringRecording = values["mute_during_recording"] as? Bool ?? result.muteDuringRecording
            if let volume = values["recording_start_sound_volume"] as? Int {
                result.recordingStartSoundVolume = min(200, max(0, volume))
            } else if values["play_recording_start_sound"] as? Bool == false {
                result.recordingStartSoundVolume = 0
            }
            if let position = values["overlay_position"] as? String,
               OverlayPosition(rawValue: position) != nil {
                result.overlayPosition = position
            }
            result.openAIModel = values["openai_model"] as? String ?? result.openAIModel
            result.elevenLabsModel = values["elevenlabs_model"] as? String ?? result.elevenLabsModel
            result.groqModel = values["groq_model"] as? String ?? result.groqModel
        }
        if !FileManager.default.fileExists(atPath: userURL.path),
           FileManager.default.fileExists(atPath: legacyUserURL.path),
           let legacy = try? Data(contentsOf: legacyUserURL) {
            try? legacy.write(to: userURL, options: .atomic)
        }
        if let data = try? Data(contentsOf: userURL),
           let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            result.apiKey = json["api_key"] as? String ?? ""
        }
        if result.apiKey.isEmpty {
            let environment = ProcessInfo.processInfo.environment
            result.apiKey = environment["ELEVENLABS_API_KEY"]
                ?? environment["GROQ_API_KEY"]
                ?? environment["OPENAI_API_KEY"]
                ?? ""
        }
        return result
    }

    func save() throws {
        try FileManager.default.createDirectory(at: Self.directory, withIntermediateDirectories: true)
        let fields = [
            "shortcut = \(jsonString(shortcut))",
            "recording_control = \(jsonString(recordingControl))",
            "recording_time_limit_minutes = \(recordingTimeLimitMinutes)",
            "log_retention_hours = \(logRetentionHours)",
            "auto_send = \(autoSend)",
            "language = \(jsonString(language))",
            "keyterms = [\(keyterms.map(jsonString).joined(separator: ", "))]",
            "replacements = [\(replacements.map(jsonString).joined(separator: ", "))]",
            "no_verbatim = \(noVerbatim)",
            "mute_during_recording = \(muteDuringRecording)",
            "recording_start_sound_volume = \(recordingStartSoundVolume)",
            "overlay_position = \(jsonString(overlayPosition))",
            "openai_model = \(jsonString(openAIModel))",
            "elevenlabs_model = \(jsonString(elevenLabsModel))",
            "groq_model = \(jsonString(groqModel))",
        ]
        try (fields.joined(separator: "\n") + "\n").write(to: Self.configURL, atomically: true, encoding: .utf8)
        var user = ((try? Data(contentsOf: Self.userURL))
            .flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }) ?? [:]
        user["api_key"] = apiKey
        let data = try JSONSerialization.data(withJSONObject: user, options: [.prettyPrinted, .sortedKeys])
        try data.write(to: Self.userURL, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: Self.userURL.path)
    }
}

extension Settings {
    /// 설정 화면에 보이는 `찾을 말 => 바꿀 말` 한 줄을 좌우 쌍으로 나눈다.
    /// 사용자가 화살표를 `->`로 쓰는 경우도 같은 뜻으로 받아 준다.
    static func parseReplacement(_ line: String) -> (from: String, to: String)? {
        for separator in ["=>", "->"] {
            guard let range = line.range(of: separator) else { continue }
            let from = line[..<range.lowerBound].trimmingCharacters(in: .whitespaces)
            let to = line[range.upperBound...].trimmingCharacters(in: .whitespaces)
            guard !from.isEmpty else { return nil }
            return (from, to)
        }
        return nil
    }

    /// 전사 결과를 붙여넣기 직전에 치환 규칙대로 고친다. 규칙은 적힌 순서대로 적용된다.
    func applyingReplacements(to text: String) -> String {
        replacements.reduce(text) { result, rule in
            guard let (from, to) = Settings.parseReplacement(rule) else { return result }
            return result.replacingOccurrences(of: from, with: to)
        }
    }
}

private func jsonString(_ value: String) -> String {
    let data = try! JSONEncoder().encode(value)
    return String(data: data, encoding: .utf8)!
}

private func parseTOML(_ source: String) -> [String: Any] {
    var values: [String: Any] = [:]
    for rawLine in source.components(separatedBy: .newlines) {
        guard let equal = rawLine.firstIndex(of: "=") else { continue }
        let key = rawLine[..<equal].trimmingCharacters(in: .whitespaces)
        if key.isEmpty || key.hasPrefix("#") || key.hasPrefix("[") { continue }
        let rawValue = String(rawLine[rawLine.index(after: equal)...])
        var value = ""
        var quoted = false
        var escaped = false
        for character in rawValue {
            if character == "#" && !quoted { break }
            value.append(character)
            if character == "\\" && quoted && !escaped {
                escaped = true
                continue
            }
            if character == "\"" && !escaped { quoted.toggle() }
            escaped = false
        }
        value = value.trimmingCharacters(in: .whitespaces)
        if value == "true" { values[key] = true }
        else if value == "false" { values[key] = false }
        else if let data = value.data(using: .utf8),
                let decoded = try? JSONSerialization.jsonObject(with: data, options: .fragmentsAllowed) {
            values[key] = decoded
        }
    }
    return values
}
