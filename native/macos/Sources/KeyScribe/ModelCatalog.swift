import Foundation

enum TranscriptionProvider {
    case openAI
    case elevenLabs

    init?(apiKey: String) {
        if apiKey.hasPrefix("sk-") { self = .openAI }
        else if apiKey.hasPrefix("sk_") { self = .elevenLabs }
        else { return nil }
    }
}

enum ModelCatalogError: LocalizedError {
    case invalidKey
    case noModels
    case invalidResponse
    case server(Int)

    var errorDescription: String? {
        switch self {
        case .invalidKey: return "OpenAI(sk-) 또는 ElevenLabs(sk_) API 키를 입력해 주세요."
        case .noModels: return "사용 가능한 전사 모델이 없습니다."
        case .invalidResponse: return "모델 목록 응답을 읽을 수 없습니다."
        case .server(let status): return "모델 목록 요청에 실패했습니다 (HTTP \(status)). API 키를 확인해 주세요."
        }
    }
}

enum ModelCatalog {
    static func fetch(apiKey: String, completion: @escaping (Result<[String], Error>) -> Void) {
        guard let provider = TranscriptionProvider(apiKey: apiKey) else {
            completion(.failure(ModelCatalogError.invalidKey))
            return
        }
        let endpoint = provider == .openAI
            ? "https://api.openai.com/v1/models"
            : "https://api.elevenlabs.io/v1/models"
        var request = URLRequest(url: URL(string: endpoint)!)
        request.timeoutInterval = 10
        if provider == .openAI {
            request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        } else {
            request.setValue(apiKey, forHTTPHeaderField: "xi-api-key")
        }
        URLSession.shared.dataTask(with: request) { data, response, error in
            let result: Result<[String], Error>
            if let error {
                result = .failure(error)
            } else if let response = response as? HTTPURLResponse,
                      !(200...299).contains(response.statusCode) {
                result = .failure(ModelCatalogError.server(response.statusCode))
            } else if let data, let models = parse(data: data, provider: provider) {
                result = models.isEmpty ? .failure(ModelCatalogError.noModels) : .success(models)
            } else {
                result = .failure(ModelCatalogError.invalidResponse)
            }
            DispatchQueue.main.async { completion(result) }
        }.resume()
    }

    static func parse(data: Data, provider: TranscriptionProvider) -> [String]? {
        let objects: [[String: Any]]
        if provider == .openAI {
            guard let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let items = root["data"] as? [[String: Any]] else { return nil }
            objects = items
        } else {
            guard let items = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] else { return nil }
            objects = items
        }
        let key = provider == .openAI ? "id" : "model_id"
        return Array(Set(objects.compactMap { $0[key] as? String }.filter { id in
            let supported = provider == .openAI
                ? (id.contains("transcribe") || id == "whisper-1")
                : id.hasPrefix("scribe")
            return supported && id.range(of: "-[0-9]{4}-[0-9]{2}-[0-9]{2}$",
                                         options: .regularExpression) == nil
        })).sorted()
    }
}
