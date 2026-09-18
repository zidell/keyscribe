import Foundation

struct Settings {
    var apiKey = ""
    var shortcut = "right_command"
    var recordingControl = "hold"
    var recordingTimeLimitMinutes = 30
    var autoSend = true
    var language = "ko"
    var keyterms: [String] = []
    var noVerbatim = true
    var muteDuringRecording = true
    var recordingStartSoundVolume = 100
    var openAIModel = "gpt-transcribe"
    var elevenLabsModel = "scribe_v2"
    var groqModel = "whisper-large-v3-turbo"

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
            result.autoSend = values["auto_send"] as? Bool ?? result.autoSend
            result.language = values["language"] as? String ?? result.language
            result.keyterms = values["keyterms"] as? [String] ?? result.keyterms
            result.noVerbatim = values["no_verbatim"] as? Bool ?? result.noVerbatim
            result.muteDuringRecording = values["mute_during_recording"] as? Bool ?? result.muteDuringRecording
            if let volume = values["recording_start_sound_volume"] as? Int {
                result.recordingStartSoundVolume = min(200, max(0, volume))
            } else if values["play_recording_start_sound"] as? Bool == false {
                result.recordingStartSoundVolume = 0
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
            "auto_send = \(autoSend)",
            "language = \(jsonString(language))",
            "keyterms = [\(keyterms.map(jsonString).joined(separator: ", "))]",
            "no_verbatim = \(noVerbatim)",
            "mute_during_recording = \(muteDuringRecording)",
            "recording_start_sound_volume = \(recordingStartSoundVolume)",
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
