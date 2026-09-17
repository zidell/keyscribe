import AppKit
import AVFoundation
import ApplicationServices
import CoreGraphics
import Foundation

private enum Phase {
    case idle, recording, transcribing
}

private let keyCodes: [String: CGKeyCode] = [
    "right_option": 61, "right_alt": 61,
    "left_option": 58, "option": 58, "left_alt": 58, "alt": 58,
    "right_ctrl": 62, "left_ctrl": 59, "ctrl": 59,
    "right_shift": 60, "left_shift": 56, "shift": 56,
    "right_cmd": 54, "left_cmd": 55, "cmd": 55,
    "enter": 36, "esc": 53, "tab": 48, "space": 49,
    "f1": 122, "f2": 120, "f3": 99, "f4": 118,
    "f5": 96, "f6": 97, "f7": 98, "f8": 100,
    "f9": 101, "f10": 109, "f11": 103, "f12": 111,
]

private func eventTapCallback(proxy: CGEventTapProxy, type: CGEventType,
                              event: CGEvent, userInfo: UnsafeMutableRawPointer?) -> Unmanaged<CGEvent>? {
    guard let userInfo else { return Unmanaged.passUnretained(event) }
    let app = Unmanaged<KeyScribeApp>.fromOpaque(userInfo).takeUnretainedValue()
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        DispatchQueue.main.async { app.enableEventTap() }
    } else if type == .flagsChanged || type == .keyDown || type == .keyUp {
        let code = CGKeyCode(event.getIntegerValueField(.keyboardEventKeycode))
        let flags = event.flags
        DispatchQueue.main.async { app.handleKeyEvent(type: type, code: code, flags: flags) }
    }
    return Unmanaged.passUnretained(event)
}

final class KeyScribeApp: NSObject, NSApplicationDelegate {
    private var settings = Settings.load()
    private let transcriber = Transcriber()
    private var phase: Phase = .idle
    private var statusItem: NSStatusItem!
    private var statusLine: NSMenuItem!
    private var lastLine: NSMenuItem!
    private var recorder: AVAudioRecorder?
    private var recordingURL: URL?
    private var eventTap: CFMachPort?
    private var keyDown = false
    private var session = UUID()
    private var wasMuted: Bool?
    private var overlay: RecordingOverlay?
    private var overlayTimer: Timer?

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        setupMenu()
        installEventTap()
        if settings.apiKey.isEmpty { showSettings(nil) }
    }

    func applicationWillTerminate(_ notification: Notification) {
        cancelRecording()
    }

    private func setupMenu() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let url = Bundle.main.url(forResource: "keyscribe-menu", withExtension: "png"),
           let icon = NSImage(contentsOf: url) {
            icon.size = NSSize(width: 18, height: 18)
            icon.isTemplate = true
            statusItem.button?.image = icon
            statusItem.button?.imagePosition = .imageOnly
        }
        let menu = NSMenu()
        statusLine = NSMenuItem(title: "준비됨", action: nil, keyEquivalent: "")
        statusLine.isEnabled = false
        menu.addItem(statusLine)
        lastLine = NSMenuItem(title: "최근 변환: 없음", action: nil, keyEquivalent: "")
        lastLine.isEnabled = false
        menu.addItem(lastLine)
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "설정…", action: #selector(showSettings(_:)), keyEquivalent: ","))
        menu.addItem(NSMenuItem(title: "설정 폴더 열기", action: #selector(openSettingsFolder(_:)), keyEquivalent: ""))
        menu.addItem(.separator())
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "개발 버전"
        let versionItem = NSMenuItem(title: "버전 \(version)", action: nil, keyEquivalent: "")
        versionItem.isEnabled = false
        menu.addItem(versionItem)
        menu.addItem(NSMenuItem(title: "종료", action: #selector(quit(_:)), keyEquivalent: "q"))
        for item in menu.items where item.action != nil { item.target = self }
        statusItem.menu = menu
    }

    private func setStatus(_ message: String) {
        statusLine.title = message
    }

    private func installEventTap() {
        let mask = (CGEventMask(1) << CGEventType.flagsChanged.rawValue)
            | (CGEventMask(1) << CGEventType.keyDown.rawValue)
            | (CGEventMask(1) << CGEventType.keyUp.rawValue)
        let context = Unmanaged.passUnretained(self).toOpaque()
        guard let tap = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .headInsertEventTap,
                                          options: .defaultTap, eventsOfInterest: mask,
                                          callback: eventTapCallback, userInfo: context) else {
            let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
            _ = AXIsProcessTrustedWithOptions(options)
            setStatus("손쉬운 사용 권한을 허용한 뒤 앱을 다시 실행해 주세요")
            return
        }
        eventTap = tap
        let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
    }

    func enableEventTap() {
        if let eventTap { CGEvent.tapEnable(tap: eventTap, enable: true) }
    }

    func handleKeyEvent(type: CGEventType, code: CGKeyCode, flags: CGEventFlags) {
        if type == .keyDown && code == 53 {
            if phase != .idle { cancelRecording() }
            return
        }
        guard code == triggerKeyCode() else { return }
        let pressed: Bool
        if type == .flagsChanged {
            let mask: CGEventFlags
            switch code {
            case 58, 61: mask = .maskAlternate
            case 59, 62: mask = .maskControl
            case 55, 54: mask = .maskCommand
            case 56, 60: mask = .maskShift
            default: return
            }
            pressed = flags.contains(mask)
        } else {
            pressed = type == .keyDown
        }
        if pressed == keyDown { return }
        keyDown = pressed
        if pressed {
            if phase == .idle { startRecording() }
            else if phase == .recording && settings.recordingControl == "toggle" { stopRecording() }
        } else if phase == .recording && settings.recordingControl == "hold" {
            stopRecording()
        }
    }

    private func triggerKeyCode() -> CGKeyCode {
        keyCodes[settings.shortcut] ?? 61
    }

    private func startRecording() {
        guard !settings.apiKey.isEmpty else { showSettings(nil); return }
        let authorization = AVCaptureDevice.authorizationStatus(for: .audio)
        if authorization == .notDetermined {
            AVCaptureDevice.requestAccess(for: .audio) { [weak self] granted in
                DispatchQueue.main.async {
                    guard let self else { return }
                    if granted {
                        if self.settings.recordingControl == "toggle" || self.keyDown { self.startRecording() }
                    } else {
                        self.setStatus("마이크 권한이 필요합니다")
                        self.showTransientOverlay(.microphoneError)
                    }
                }
            }
            return
        }
        guard authorization == .authorized else {
            setStatus("마이크 권한이 필요합니다")
            showTransientOverlay(.microphoneError)
            return
        }
        session = UUID()
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("keyscribe-\(session.uuidString).wav")
        let format: [String: Any] = [
            AVFormatIDKey: Int(kAudioFormatLinearPCM), AVSampleRateKey: 16_000,
            AVNumberOfChannelsKey: 1, AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false, AVLinearPCMIsBigEndianKey: false,
        ]
        do {
            recorder = try AVAudioRecorder(url: url, settings: format)
            recorder?.isMeteringEnabled = true
            guard recorder?.record() == true else { throw NSError(domain: "KeyScribe", code: 1,
                userInfo: [NSLocalizedDescriptionKey: "마이크를 시작하지 못했습니다."]) }
            recordingURL = url
            phase = .recording
            setStatus("녹음 중 · Esc 취소")
            showActiveOverlay(.recording)
            if settings.muteDuringRecording { muteSystemAudio() }
        } catch {
            recorder = nil
            try? FileManager.default.removeItem(at: url)
            setStatus("녹음 오류: \(error.localizedDescription)")
            showTransientOverlay(.microphoneError)
        }
    }

    private func stopRecording() {
        guard phase == .recording, let url = recordingURL else { return }
        recorder?.stop()
        recorder = nil
        restoreSystemAudio()
        phase = .transcribing
        setStatus("변환 중 · Esc 취소")
        showActiveOverlay(.transcribing)
        let currentSession = session
        let byteCount = (try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
        if settings.apiKey.hasPrefix("sk-") && byteCount > 24 * 1024 * 1024 {
            finish(.failure(NSError(domain: "KeyScribe", code: 2,
                userInfo: [NSLocalizedDescriptionKey: "OpenAI 녹음 크기 제한(24 MB)을 초과했습니다."])),
                   url: url, session: currentSession)
            return
        }
        transcriber.transcribe(audioURL: url, settings: settings) { [weak self] result in
            DispatchQueue.main.async { self?.finish(result, url: url, session: currentSession) }
        }
    }

    private func finish(_ result: Result<String, Error>, url: URL, session completedSession: UUID) {
        try? FileManager.default.removeItem(at: url)
        guard completedSession == session && phase == .transcribing else { return }
        recordingURL = nil
        phase = .idle
        switch result {
        case .success(let text):
            hideOverlay()
            guard !text.isEmpty else { setStatus("인식된 음성이 없습니다"); return }
            lastLine.title = "최근 변환: \(String(text.prefix(60)))"
            paste(text)
            setStatus("완료")
        case .failure(let error):
            setStatus(error.localizedDescription)
            showTransientOverlay(.failed)
        }
    }

    private func cancelRecording() {
        session = UUID()
        transcriber.cancel()
        recorder?.stop()
        recorder = nil
        if let recordingURL { try? FileManager.default.removeItem(at: recordingURL) }
        recordingURL = nil
        restoreSystemAudio()
        phase = .idle
        if statusLine != nil { setStatus("취소됨") }
        showTransientOverlay(.cancelled)
    }

    private func showActiveOverlay(_ state: RecordingOverlay.State) {
        if overlay == nil { overlay = RecordingOverlay() }
        overlay?.show(state)
        overlayTimer?.invalidate()
        let timer = Timer(timeInterval: 0.05, repeats: true) { [weak self] _ in
            guard let self else { return }
            let level: CGFloat?
            if self.phase == .recording, let recorder = self.recorder {
                recorder.updateMeters()
                let power = Double(recorder.averagePower(forChannel: 0))
                level = CGFloat(min(1, pow(10.0, power / 20.0) * 8.0))
            } else {
                level = nil
            }
            self.overlay?.tick(level: level)
        }
        overlayTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }

    private func showTransientOverlay(_ state: RecordingOverlay.State) {
        overlayTimer?.invalidate()
        overlayTimer = nil
        if overlay == nil { overlay = RecordingOverlay() }
        overlay?.show(state)
        let currentSession = session
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { [weak self] in
            guard let self, self.session == currentSession, self.phase == .idle else { return }
            self.overlay?.hide()
        }
    }

    private func hideOverlay() {
        overlayTimer?.invalidate()
        overlayTimer = nil
        overlay?.hide()
    }

    private func paste(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        guard let source = CGEventSource(stateID: .hidSystemState),
              let down = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: true),
              let up = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: false) else { return }
        down.flags = .maskCommand
        up.flags = .maskCommand
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
        if settings.autoSend {
            // Some editors apply pasted text asynchronously. Give them time to finish
            // before sending Return, and hold the key briefly like a physical press.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) {
                guard let enterDown = CGEvent(keyboardEventSource: source, virtualKey: 36, keyDown: true),
                      let enterUp = CGEvent(keyboardEventSource: source, virtualKey: 36, keyDown: false) else { return }
                enterDown.flags = []
                enterUp.flags = []
                enterDown.post(tap: .cghidEventTap)
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.04) {
                    enterUp.post(tap: .cghidEventTap)
                }
            }
        }
    }

    private func runAppleScript(_ script: String) -> String? {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        task.arguments = ["-e", script]
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = Pipe()
        guard (try? task.run()) != nil else { return nil }
        task.waitUntilExit()
        guard task.terminationStatus == 0 else { return nil }
        return String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func muteSystemAudio() {
        guard let state = runAppleScript("output muted of (get volume settings)") else { return }
        wasMuted = state == "true"
        if wasMuted == false { _ = runAppleScript("set volume output muted true") }
    }

    private func restoreSystemAudio() {
        if wasMuted == false { _ = runAppleScript("set volume output muted false") }
        wasMuted = nil
    }

    @objc private func openSettingsFolder(_ sender: Any?) {
        NSWorkspace.shared.open(Settings.directory)
    }

    @objc private func quit(_ sender: Any?) { NSApp.terminate(nil) }

    @objc private func showSettings(_ sender: Any?) {
        let dialog = SettingsDialog(settings: settings)
        guard let updated = dialog.present() else { return }
        do {
            try updated.save()
            settings = updated
            keyDown = false
            setStatus("설정 저장됨")
        } catch {
            setStatus("설정 저장 실패: \(error.localizedDescription)")
        }
    }
}

let application = NSApplication.shared
let delegate = KeyScribeApp()
application.delegate = delegate
application.run()
