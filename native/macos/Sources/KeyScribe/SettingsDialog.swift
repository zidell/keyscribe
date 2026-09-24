import AppKit
import Foundation

final class SettingsDialog: NSObject, NSTextFieldDelegate {
    private static let elevenLabsAPIKeysURL = URL(string: "https://elevenlabs.io/app/developers/api-keys")!
    private static let openAIAPIKeysURL = URL(string: "https://platform.openai.com/api-keys")!
    private static let groqAPIKeysURL = URL(string: "https://console.groq.com/keys")!
    private let original: Settings
    private var selectedModels: [TranscriptionProvider: String]
    private var shownProvider: TranscriptionProvider?
    private var requestID = UUID()
    private var pasteboardChangeCount = NSPasteboard.general.changeCount

    private let apiKey = NSSecureTextField()
    private let model = NSPopUpButton()
    private let refreshButton = NSButton()
    private let modelHint = NSTextField(labelWithString: "")
    private let language = NSPopUpButton()
    private let shortcut = NSPopUpButton()
    private let recordingControl = NSPopUpButton()
    private let recordingTimeLimit = NSPopUpButton()
    private let overlayPosition = NSPopUpButton()
    private let logRetention = NSPopUpButton()
    private let keyterms = NSTextView()
    private let replacements = NSTextView()
    private let noVerbatim = NSButton(checkboxWithTitle: "군더더기 말 제거 (ElevenLabs)", target: nil, action: nil)
    private let mute = NSButton(checkboxWithTitle: "녹음 중 시스템 소리 음소거", target: nil, action: nil)
    private let recordingStartSoundVolume = NSSlider()
    private let recordingStartSoundValue = NSTextField(labelWithString: "100%")
    private let autoSend = NSButton(checkboxWithTitle: "붙여넣은 뒤 Enter 입력", target: nil, action: nil)

    init(settings: Settings) {
        original = settings
        selectedModels = [.openAI: settings.openAIModel, .elevenLabs: settings.elevenLabsModel,
                          .groq: settings.groqModel]
        super.init()
    }

    func present() -> Settings? {
        let alert = NSAlert()
        alert.messageText = "KeyScribe 설정"
        alert.informativeText = "API 키는 이 컴퓨터의 사용자 설정에만 저장됩니다."
        alert.addButton(withTitle: "저장")
        alert.addButton(withTitle: "취소")
        alert.accessoryView = makeForm()
        NSApp.activate(ignoringOtherApps: true)
        refreshModels(nil)
        guard alert.runModal() == .alertFirstButtonReturn else {
            requestID = UUID()
            return nil
        }
        requestID = UUID()
        if let provider = shownProvider, let selected = model.titleOfSelectedItem {
            selectedModels[provider] = selected
        }
        var updated = original
        updated.apiKey = apiKey.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        updated.openAIModel = selectedModels[.openAI] ?? original.openAIModel
        updated.elevenLabsModel = selectedModels[.elevenLabs] ?? original.elevenLabsModel
        updated.groqModel = selectedModels[.groq] ?? original.groqModel
        updated.language = language.selectedItem?.representedObject as? String ?? original.language
        updated.shortcut = shortcut.selectedItem?.representedObject as? String ?? original.shortcut
        updated.recordingControl = recordingControl.selectedItem?.representedObject as? String ?? original.recordingControl
        updated.recordingTimeLimitMinutes = Int(recordingTimeLimit.selectedItem?.representedObject as? String ?? "30") ?? 30
        updated.logRetentionHours = Int(logRetention.selectedItem?.representedObject as? String ?? "")
            ?? original.logRetentionHours
        updated.overlayPosition = overlayPosition.selectedItem?.representedObject as? String ?? original.overlayPosition
        updated.keyterms = keyterms.string.components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        updated.replacements = replacements.string.components(separatedBy: .newlines)
            .compactMap(Settings.parseReplacement)
            .prefix(100)
            .map { "\($0.from) => \($0.to)" }
        updated.noVerbatim = noVerbatim.state == .on
        updated.muteDuringRecording = mute.state == .on
        updated.recordingStartSoundVolume = Int(recordingStartSoundVolume.doubleValue.rounded())
        updated.autoSend = autoSend.state == .on
        return updated
    }

    private func makeForm() -> NSView {
        let width: CGFloat = 480
        let form = NSView(frame: NSRect(x: 0, y: 0, width: width, height: 620))
        func addLabel(_ title: String, y: CGFloat, height: CGFloat = 24) {
            let label = NSTextField(labelWithString: title)
            label.frame = NSRect(x: 0, y: y, width: 124, height: height)
            form.addSubview(label)
        }
        func addPicker(_ picker: NSPopUpButton, y: CGFloat, width: CGFloat = 350) {
            picker.frame = NSRect(x: 125, y: y, width: width, height: 26)
            form.addSubview(picker)
        }
        func populate(_ picker: NSPopUpButton, options: [(String, String)], selected: String) {
            picker.removeAllItems()
            var items = options
            if !items.contains(where: { $0.0 == selected }) { items.insert((selected, selected), at: 0) }
            for (code, name) in items {
                picker.addItem(withTitle: name)
                picker.lastItem?.representedObject = code
            }
            if let index = items.firstIndex(where: { $0.0 == selected }) {
                picker.selectItem(at: index)
            }
        }

        addLabel("API 키", y: 452)
        apiKey.frame = NSRect(x: 125, y: 452, width: 350, height: 24)
        apiKey.stringValue = original.apiKey
        apiKey.placeholderString = "sk_… (ElevenLabs), sk-… (OpenAI), gsk_… (Groq)"
        apiKey.delegate = self
        form.addSubview(apiKey)
        addLabel("API 키 발급", y: 431, height: 18)
        func addKeyLink(_ title: String, x: CGFloat, width: CGFloat, action: Selector) {
            let button = NSButton(title: title, target: self, action: action)
            button.frame = NSRect(x: x, y: 431, width: width, height: 18)
            button.isBordered = false
            button.contentTintColor = .linkColor
            button.font = .systemFont(ofSize: 11)
            form.addSubview(button)
        }
        addKeyLink("ElevenLabs 키 ↗", x: 125, width: 115, action: #selector(openElevenLabsAPIKeys(_:)))
        addKeyLink("OpenAI 키 ↗", x: 245, width: 105, action: #selector(openOpenAIAPIKeys(_:)))
        addKeyLink("Groq 키 ↗", x: 355, width: 100, action: #selector(openGroqAPIKeys(_:)))

        addLabel("전사 모델", y: 399)
        addPicker(model, y: 398, width: 255)
        model.target = self
        model.action = #selector(modelSelectionChanged(_:))
        refreshButton.frame = NSRect(x: 385, y: 398, width: 90, height: 26)
        refreshButton.title = "새로고침"
        refreshButton.target = self
        refreshButton.action = #selector(refreshModels(_:))
        form.addSubview(refreshButton)
        modelHint.frame = NSRect(x: 125, y: 376, width: 350, height: 18)
        modelHint.font = .systemFont(ofSize: 11)
        modelHint.textColor = .secondaryLabelColor
        form.addSubview(modelHint)

        addLabel("인식 언어", y: 348)
        let languageCodes = "af ar hy az be bs bg ca zh hr cs da nl en et fi fr gl de el he hi hu id it ja kn kk ko lv lt mk ms mr mi ne no fa pl pt ro ru sr sk sl es sw sv tl ta th tr uk ur vi cy"
            .split(separator: " ").map(String.init)
        let languageOptions = languageCodes.map { code in
            (code, Locale.current.localizedString(forLanguageCode: code) ?? code)
        }.sorted { $0.1.localizedStandardCompare($1.1) == .orderedAscending }
        populate(language, options: languageOptions, selected: original.language)
        addPicker(language, y: 346)

        addLabel("녹음 단축키", y: 308)
        let shortcuts = [
            ("right_option", "오른쪽 Option (⌥)"), ("left_option", "왼쪽 Option (⌥)"),
            ("right_command", "오른쪽 Command (⌘)"), ("left_cmd", "왼쪽 Command (⌘)"),
            ("right_ctrl", "오른쪽 Control (⌃)"), ("left_ctrl", "왼쪽 Control (⌃)"),
            ("right_shift", "오른쪽 Shift (⇧)"), ("left_shift", "왼쪽 Shift (⇧)"),
        ]
        populate(shortcut, options: shortcuts, selected: original.shortcut)
        addPicker(shortcut, y: 306)

        addLabel("녹음 방식", y: 268)
        populate(recordingControl, options: [
            ("hold", "누르는 동안 녹음"), ("toggle", "한번 누르면 녹음시작, 다시 누르면 종료"),
        ], selected: original.recordingControl)
        addPicker(recordingControl, y: 266)

        for view in form.subviews {
            var frame = view.frame
            frame.origin.y += 40
            view.frame = frame
        }
        addLabel("녹음 시간 제한", y: 268)
        populate(recordingTimeLimit, options: [
            ("10", "10분"), ("20", "20분"), ("30", "30분"), ("60", "60분"),
        ], selected: String(original.recordingTimeLimitMinutes))
        addPicker(recordingTimeLimit, y: 266, width: 110)
        let retentionLabel = NSTextField(labelWithString: "로그·녹음 보존")
        retentionLabel.frame = NSRect(x: 250, y: 268, width: 95, height: 24)
        form.addSubview(retentionLabel)
        populate(logRetention, options: [
            ("1", "1시간"), ("24", "1일"), ("168", "7일"), ("720", "30일"),
        ], selected: String(original.logRetentionHours))
        logRetention.frame = NSRect(x: 345, y: 266, width: 130, height: 26)
        form.addSubview(logRetention)

        for view in form.subviews {
            var frame = view.frame
            frame.origin.y += 40
            view.frame = frame
        }
        addLabel("녹음 위젯 위치", y: 268)
        populate(overlayPosition,
                 options: OverlayPosition.allCases.map { ($0.rawValue, $0.title) },
                 selected: original.overlayPosition)
        addPicker(overlayPosition, y: 266)

        for view in form.subviews {
            var frame = view.frame
            frame.origin.y += 50
            view.frame = frame
        }
        func addTextArea(_ view: NSTextView, title: String, lines: [String], y: CGFloat,
                         labelY: CGFloat? = nil, labelHeight: CGFloat = 52) {
            addLabel(title, y: labelY ?? (y + 2), height: labelHeight)
            let scroll = NSScrollView(frame: NSRect(x: 125, y: y, width: 350, height: 58))
            scroll.hasVerticalScroller = true
            scroll.borderType = .bezelBorder
            view.frame = NSRect(x: 0, y: 0, width: 350, height: 58)
            view.isVerticallyResizable = true
            view.autoresizingMask = [.width]
            view.string = lines.joined(separator: "\n")
            view.isRichText = false
            scroll.documentView = view
            form.addSubview(scroll)
        }
        addTextArea(keyterms, title: "인식 단어\n(한 줄에 하나)", lines: original.keyterms, y: 249)
        addTextArea(replacements, title: "치환 단어\n(찾을 말 => 바꿀 말)",
                    lines: original.replacements, y: 185, labelY: 205, labelHeight: 40)
        let help = NSButton(title: "사용법", target: self,
                            action: #selector(showReplacementHelp(_:)))
        help.frame = NSRect(x: 0, y: 186, width: 124, height: 18)
        help.isBordered = false
        help.contentTintColor = .linkColor
        help.font = .systemFont(ofSize: 11)
        help.alignment = .left
        form.addSubview(help)

        noVerbatim.frame = NSRect(x: 125, y: 154, width: 350, height: 25)
        noVerbatim.state = original.noVerbatim ? .on : .off
        form.addSubview(noVerbatim)
        mute.frame = NSRect(x: 125, y: 116, width: 350, height: 25)
        mute.state = original.muteDuringRecording ? .on : .off
        form.addSubview(mute)
        addLabel("녹음 시작 효과음", y: 40)
        recordingStartSoundVolume.frame = NSRect(x: 125, y: 40, width: 280, height: 25)
        recordingStartSoundVolume.minValue = 0
        recordingStartSoundVolume.maxValue = 200
        recordingStartSoundVolume.doubleValue = Double(original.recordingStartSoundVolume)
        recordingStartSoundVolume.target = self
        recordingStartSoundVolume.action = #selector(recordingStartSoundVolumeChanged(_:))
        form.addSubview(recordingStartSoundVolume)
        recordingStartSoundValue.frame = NSRect(x: 415, y: 40, width: 60, height: 25)
        recordingStartSoundValue.stringValue = "\(original.recordingStartSoundVolume)%"
        form.addSubview(recordingStartSoundValue)
        autoSend.frame = NSRect(x: 125, y: 78, width: 350, height: 25)
        autoSend.state = original.autoSend ? .on : .off
        form.addSubview(autoSend)

        updateProvider()
        return form
    }

    @objc private func recordingStartSoundVolumeChanged(_ sender: NSSlider) {
        recordingStartSoundValue.stringValue = "\(Int(sender.doubleValue.rounded()))%"
    }

    private func updateProvider() {
        let provider = TranscriptionProvider(apiKey: apiKey.stringValue.trimmingCharacters(in: .whitespacesAndNewlines))
        if provider != shownProvider {
            if let shownProvider, let selected = model.titleOfSelectedItem {
                selectedModels[shownProvider] = selected
            }
            shownProvider = provider
            model.removeAllItems()
            if let provider, let selected = selectedModels[provider] {
                model.addItem(withTitle: selected)
                model.selectItem(at: 0)
            }
        }
        model.isEnabled = provider != nil
        refreshButton.isEnabled = provider != nil
        updateNoVerbatim()
    }

    private func updateNoVerbatim() {
        let selected = model.titleOfSelectedItem ?? ""
        noVerbatim.isEnabled = shownProvider == .elevenLabs
            && (selected == "scribe_v2" || selected == "scribe_v2_medical")
    }

    @objc private func modelSelectionChanged(_ sender: Any?) {
        if let shownProvider, let selected = model.titleOfSelectedItem {
            selectedModels[shownProvider] = selected
        }
        updateNoVerbatim()
    }

    @objc private func refreshModels(_ sender: Any?) {
        let key = apiKey.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let preserveCurrentModel = sender == nil
        updateProvider()
        guard TranscriptionProvider(apiKey: key) != nil else {
            modelHint.stringValue = ModelCatalogError.invalidKey.localizedDescription
            modelHint.textColor = .secondaryLabelColor
            return
        }
        let request = UUID()
        requestID = request
        modelHint.stringValue = "사용 가능한 전사 모델을 불러오는 중…"
        modelHint.textColor = .secondaryLabelColor
        refreshButton.isEnabled = false
        ModelCatalog.fetch(apiKey: key) { [weak self] result in
            guard let self, self.requestID == request,
                  self.apiKey.stringValue.trimmingCharacters(in: .whitespacesAndNewlines) == key else { return }
            self.refreshButton.isEnabled = true
            switch result {
            case .success(let models):
                let previous = self.model.titleOfSelectedItem
                var displayedModels = models
                if preserveCurrentModel, let previous, !models.contains(previous) {
                    displayedModels.insert(previous, at: 0)
                }
                self.model.removeAllItems()
                self.model.addItems(withTitles: displayedModels)
                if let previous, displayedModels.contains(previous) { self.model.selectItem(withTitle: previous) }
                else { self.model.selectItem(at: 0) }
                self.modelHint.stringValue = "전사 모델 \(models.count)개"
                self.modelHint.textColor = .secondaryLabelColor
                self.modelSelectionChanged(nil)
            case .failure(let error):
                self.modelHint.stringValue = error.localizedDescription
                self.modelHint.textColor = .systemRed
            }
        }
    }

    func controlTextDidChange(_ notification: Notification) {
        guard notification.object as? NSTextField === apiKey else { return }
        requestID = UUID()
        updateProvider()
        modelHint.stringValue = "API 키 변경 후 새로고침을 눌러 주세요."
        modelHint.textColor = .secondaryLabelColor
        let currentChangeCount = NSPasteboard.general.changeCount
        if currentChangeCount != pasteboardChangeCount {
            pasteboardChangeCount = currentChangeCount
            DispatchQueue.main.async { [weak self] in self?.refreshModels(nil) }
        }
    }

    func controlTextDidEndEditing(_ notification: Notification) {
        guard notification.object as? NSTextField === apiKey else { return }
        refreshModels(nil)
    }

    @objc private func showReplacementHelp(_ sender: Any?) {
        let alert = NSAlert()
        alert.messageText = "치환 단어 사용법"
        alert.informativeText = Self.replacementHelp
        alert.addButton(withTitle: "닫기")
        alert.runModal()
    }

    private static let replacementHelp = """
        인식된 문장을 붙여넣기 직전에 고칩니다. 한 줄에 규칙 하나씩, \
        "찾을 말 => 바꿀 말" 형식으로 적습니다. => 대신 ->도 됩니다.

            비디오 스튜 => VideoStew
            지피티 => GPT
            음 =>                      (바꿀 말을 비우면 그 말을 지웁니다)

        • => 앞뒤 공백은 알아서 정리합니다.
        • 규칙은 적힌 순서대로 차례로 적용됩니다.

        ■ 키 입력 넣기

        바꿀 말에 대괄호로 키 이름을 적으면, 그 자리에서 실제로 그 키를 누릅니다.

            전송해줘 => [enter]
            검색창 => [cmd+k]
            목록으로 => 첫째[enter]둘째[enter]셋째

        쓸 수 있는 키
            \(KeyToken.keyNames)

        조합키
            cmd, ctrl, alt, shift 를 +로 이어 씁니다. 예) [cmd+shift+p]

        • 대소문자와 공백은 따지지 않습니다. [Cmd + K], [cmd-k], [Page Up] 모두 됩니다.
        • a~z, 0~9은 조합키와 같이 쓸 때만 키로 봅니다. [k]는 그냥 글자로 붙습니다.
        • 모르는 이름이면 키로 보지 않고 대괄호째 그대로 붙여넣습니다.
        • 키를 섞으면 조각마다 잠깐 기다렸다 이어서 붙이므로, 길면 조금 느립니다.
        """

    @objc private func openElevenLabsAPIKeys(_ sender: Any?) {
        NSWorkspace.shared.open(Self.elevenLabsAPIKeysURL)
    }

    @objc private func openOpenAIAPIKeys(_ sender: Any?) {
        NSWorkspace.shared.open(Self.openAIAPIKeysURL)
    }

    @objc private func openGroqAPIKeys(_ sender: Any?) {
        NSWorkspace.shared.open(Self.groqAPIKeysURL)
    }
}
