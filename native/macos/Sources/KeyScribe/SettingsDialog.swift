import AppKit
import Foundation

final class SettingsDialog: NSObject, NSTextFieldDelegate {
    private static let elevenLabsAPIKeysURL = URL(string: "https://elevenlabs.io/app/developers/api-keys")!
    private static let openAIAPIKeysURL = URL(string: "https://platform.openai.com/api-keys")!
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
    private let keyterms = NSTextView()
    private let noVerbatim = NSButton(checkboxWithTitle: "군더더기 말 제거 (ElevenLabs)", target: nil, action: nil)
    private let mute = NSButton(checkboxWithTitle: "녹음 중 시스템 소리 음소거", target: nil, action: nil)
    private let recordingStartSoundVolume = NSSlider()
    private let recordingStartSoundValue = NSTextField(labelWithString: "100%")
    private let autoSend = NSButton(checkboxWithTitle: "붙여넣은 뒤 Enter 입력", target: nil, action: nil)

    init(settings: Settings) {
        original = settings
        selectedModels = [.openAI: settings.openAIModel, .elevenLabs: settings.elevenLabsModel]
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
        updated.language = language.selectedItem?.representedObject as? String ?? original.language
        updated.shortcut = shortcut.selectedItem?.representedObject as? String ?? original.shortcut
        updated.recordingControl = recordingControl.selectedItem?.representedObject as? String ?? original.recordingControl
        updated.keyterms = keyterms.string.components(separatedBy: .newlines)
            .map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        updated.noVerbatim = noVerbatim.state == .on
        updated.muteDuringRecording = mute.state == .on
        updated.recordingStartSoundVolume = Int(recordingStartSoundVolume.doubleValue.rounded())
        updated.autoSend = autoSend.state == .on
        return updated
    }

    private func makeForm() -> NSView {
        let width: CGFloat = 480
        let form = NSView(frame: NSRect(x: 0, y: 0, width: width, height: 490))
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
        apiKey.placeholderString = "sk_… (ElevenLabs) 또는 sk-… (OpenAI)"
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
        addKeyLink("ElevenLabs 키 받기 ↗", x: 125, width: 170, action: #selector(openElevenLabsAPIKeys(_:)))
        addKeyLink("OpenAI 키 받기 ↗", x: 305, width: 170, action: #selector(openOpenAIAPIKeys(_:)))

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

        addLabel("고유명사\n(한 줄에 하나)", y: 195, height: 52)
        let scroll = NSScrollView(frame: NSRect(x: 125, y: 193, width: 350, height: 69))
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        keyterms.frame = NSRect(x: 0, y: 0, width: 350, height: 69)
        keyterms.isVerticallyResizable = true
        keyterms.autoresizingMask = [.width]
        keyterms.string = original.keyterms.joined(separator: "\n")
        keyterms.isRichText = false
        scroll.documentView = keyterms
        form.addSubview(scroll)

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

    @objc private func openElevenLabsAPIKeys(_ sender: Any?) {
        NSWorkspace.shared.open(Self.elevenLabsAPIKeysURL)
    }

    @objc private func openOpenAIAPIKeys(_ sender: Any?) {
        NSWorkspace.shared.open(Self.openAIAPIKeysURL)
    }
}
