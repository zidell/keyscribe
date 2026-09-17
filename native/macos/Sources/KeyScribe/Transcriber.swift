import Foundation

enum TranscriptionError: LocalizedError {
    case missingKey
    case invalidResponse
    case server(Int, String)

    var errorDescription: String? {
        switch self {
        case .missingKey: return "API 키를 설정해 주세요."
        case .invalidResponse: return "전사 응답을 읽을 수 없습니다."
        case .server(let status, let message): return "전사 실패 (HTTP \(status)): \(message)"
        }
    }
}

final class Transcriber {
    private var task: URLSessionUploadTask?
    private var bodyURL: URL?
    private var generation = UUID()

    func cancel() {
        generation = UUID()
        task?.cancel()
        task = nil
        if let bodyURL { try? FileManager.default.removeItem(at: bodyURL) }
        bodyURL = nil
    }

    func transcribe(audioURL: URL, settings: Settings,
                    completion: @escaping (Result<String, Error>) -> Void) {
        cancel()
        guard !settings.apiKey.isEmpty else {
            completion(.failure(TranscriptionError.missingKey))
            return
        }
        let openAI = settings.apiKey.hasPrefix("sk-")
        let endpoint = openAI
            ? "https://api.openai.com/v1/audio/transcriptions"
            : "https://api.elevenlabs.io/v1/speech-to-text"
        let boundary = "KeyScribe-\(UUID().uuidString)"
        let fields = makeFields(settings: settings, openAI: openAI)
        let body: URL
        do {
            body = try makeMultipartBody(audioURL: audioURL, boundary: boundary, fields: fields)
        } catch {
            completion(.failure(error))
            return
        }
        bodyURL = body
        let currentGeneration = generation
        var request = URLRequest(url: URL(string: endpoint)!)
        request.httpMethod = "POST"
        request.timeoutInterval = 60
        request.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")
        if openAI {
            request.setValue("Bearer \(settings.apiKey)", forHTTPHeaderField: "Authorization")
        } else {
            request.setValue(settings.apiKey, forHTTPHeaderField: "xi-api-key")
        }
        perform(request: request, body: body, attempt: 1, generation: currentGeneration,
                completion: completion)
    }

    private func perform(request: URLRequest, body: URL, attempt: Int, generation: UUID,
                         completion: @escaping (Result<String, Error>) -> Void) {
        DebugLog.shared.record("transcription request provider=\(request.url?.host ?? "unknown") attempt=\(attempt)")
        task = URLSession.shared.uploadTask(with: request, fromFile: body) { [weak self] data, response, error in
            DispatchQueue.main.async { [weak self] in
            guard let self, self.generation == generation else { return }
            if let error {
                DebugLog.shared.record("transcription transport failure attempt=\(attempt) code=\((error as NSError).code)")
                if (error as NSError).code == NSURLErrorCancelled { return }
                if attempt == 1 {
                    self.retry(request: request, body: body, generation: generation,
                               completion: completion)
                } else {
                    self.finish(body: body, result: .failure(error), completion: completion)
                }
                return
            }
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            DebugLog.shared.record("transcription HTTP \(status) attempt=\(attempt)")
            if (status == 408 || status == 429 || (500...599).contains(status)) && attempt == 1 {
                self.retry(request: request, body: body, generation: generation,
                           completion: completion)
                return
            }
            guard (200...299).contains(status) else {
                let message = String(data: data ?? Data(), encoding: .utf8) ?? ""
                self.finish(body: body, result: .failure(TranscriptionError.server(status, String(message.prefix(300)))), completion: completion)
                return
            }
            guard let data,
                  let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let text = json["text"] as? String else {
                if attempt == 1 {
                    self.retry(request: request, body: body, generation: generation,
                               completion: completion)
                } else {
                    self.finish(body: body, result: .failure(TranscriptionError.invalidResponse), completion: completion)
                }
                return
            }
            self.finish(body: body, result: .success(cleanText(text)), completion: completion)
            }
        }
        task?.resume()
    }

    private func retry(request: URLRequest, body: URL, generation: UUID,
                       completion: @escaping (Result<String, Error>) -> Void) {
        DebugLog.shared.record("transcription retry scheduled after 1s")
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
            guard let self, self.generation == generation else { return }
            self.perform(request: request, body: body, attempt: 2, generation: generation,
                         completion: completion)
        }
    }

    private func finish(body: URL, result: Result<String, Error>,
                        completion: @escaping (Result<String, Error>) -> Void) {
        try? FileManager.default.removeItem(at: body)
        bodyURL = nil
        task = nil
        completion(result)
    }
}

private func makeFields(settings: Settings, openAI: Bool) -> [(String, String)] {
    if openAI {
        var fields = [("model", settings.openAIModel), ("language", settings.language)]
        if !settings.keyterms.isEmpty {
            fields.append(("prompt", "이 녹음에는 다음 용어가 포함됩니다: \(settings.keyterms.joined(separator: ", "))."))
        }
        return fields
    }
    var fields = [("model_id", settings.elevenLabsModel), ("language_code", settings.language)]
    for term in settings.keyterms { fields.append(("keyterms", term)) }
    if settings.elevenLabsModel == "scribe_v2" || settings.elevenLabsModel == "scribe_v2_medical" {
        fields.append(("no_verbatim", settings.noVerbatim ? "true" : "false"))
    }
    return fields
}

private func makeMultipartBody(audioURL: URL, boundary: String,
                               fields: [(String, String)]) throws -> URL {
    let output = FileManager.default.temporaryDirectory.appendingPathComponent("keyscribe-\(UUID().uuidString).multipart")
    FileManager.default.createFile(atPath: output.path, contents: nil)
    let destination = try FileHandle(forWritingTo: output)
    defer { try? destination.close() }
    for (name, value) in fields {
        try destination.write(contentsOf: Data("--\(boundary)\r\nContent-Disposition: form-data; name=\"\(name)\"\r\n\r\n\(value)\r\n".utf8))
    }
    try destination.write(contentsOf: Data("--\(boundary)\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\nContent-Type: audio/wav\r\n\r\n".utf8))
    let source = try FileHandle(forReadingFrom: audioURL)
    defer { try? source.close() }
    while let chunk = try source.read(upToCount: 64 * 1024), !chunk.isEmpty {
        try destination.write(contentsOf: chunk)
    }
    try destination.write(contentsOf: Data("\r\n--\(boundary)--\r\n".utf8))
    return output
}

private func cleanText(_ text: String) -> String {
    let regex = try! NSRegularExpression(pattern: "\\[.*?\\]")
    let range = NSRange(text.startIndex..<text.endIndex, in: text)
    let stripped = regex.stringByReplacingMatches(in: text, range: range, withTemplate: "")
    return stripped.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
}
