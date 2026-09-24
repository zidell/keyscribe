import AppKit
import AVFoundation
import ApplicationServices
import Carbon.HIToolbox
import CoreGraphics
import Foundation

private enum Phase {
    case idle, recording, transcribing
}

/// 녹음 한 번을 전사하는 작업. 녹음이 끝나는 대로 하나씩 생기고, 전사는 서로
/// 겹쳐 돌 수 있지만 결과는 녹음한 순서대로 붙여넣는다.
private final class TranscriptionJob {
    let transcriber = Transcriber()
    var result: Result<String, Error>?
}

private let keyCodes: [String: CGKeyCode] = [
    "right_option": 61, "right_alt": 61,
    "left_option": 58, "option": 58, "left_alt": 58, "alt": 58,
    "right_ctrl": 62, "left_ctrl": 59, "ctrl": 59,
    "right_shift": 60, "left_shift": 56, "shift": 56,
    "right_command": 54, "right_cmd": 54, "left_cmd": 55, "cmd": 55,
    "enter": 36, "esc": 53, "tab": 48, "space": 49,
    "f1": 122, "f2": 120, "f3": 99, "f4": 118,
    "f5": 96, "f6": 97, "f7": 98, "f8": 100,
    "f9": 101, "f10": 109, "f11": 103, "f12": 111,
]

private func eventTapCallback(proxy: CGEventTapProxy, type: CGEventType,
                              event: CGEvent, userInfo: UnsafeMutableRawPointer?) -> Unmanaged<CGEvent>? {
    guard let userInfo else { return Unmanaged.passUnretained(event) }
    let app = Unmanaged<KeyScribeApp>.fromOpaque(userInfo).takeUnretainedValue()
    var consumed = false
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        DispatchQueue.main.async { app.enableEventTap() }
    } else if type == .flagsChanged || type == .keyDown || type == .keyUp {
        let code = CGKeyCode(event.getIntegerValueField(.keyboardEventKeycode))
        let flags = event.flags
        // The tap source runs on the main run loop, so the phase behind this
        // decision is the same one the handler below will act on.
        consumed = app.consumesEscape(type: type, code: code)
        DispatchQueue.main.async { app.handleKeyEvent(type: type, code: code, flags: flags) }
    }
    return consumed ? nil : Unmanaged.passUnretained(event)
}

final class KeyScribeApp: NSObject, NSApplicationDelegate {
    private var settings = Settings.load()
    private var jobs: [TranscriptionJob] = []
    /// 앞선 결과가 붙여넣기 전에 실패했다면, 모든 작업이 끝났을 때 알린다.
    private var pendingFailure: String?
    private var pasteQueue: [PasteSegment] = []
    private var pasteScheduled = false
    private var phase: Phase = .idle {
        didSet {
            guard oldValue != phase else { return }
            updateStatusIcon()
            guard (oldValue == .idle) != (phase == .idle) else { return }
            if phase == .idle { unregisterEscapeHotKeys() } else { registerEscapeHotKeys() }
        }
    }
    private var statusItem: NSStatusItem!
    private var statusLine: NSMenuItem!
    private var recorder: AVAudioRecorder?
    private var recordingStartSound: AVAudioPlayer?
    private var recordingLimitSound: AVAudioPlayer?
    private var recordingLimitTimer: Timer?
    private var recordingURL: URL?
    private var recordingStartedAt: TimeInterval?
    private var recordingLimitMinutes = 30
    private var recordingSecondsShown: Int?
    private var eventTap: CFMachPort?
    private var escapeMonitor: Any?
    private var escapeHotKeys: [EventHotKeyRef] = []
    private var escapeHotKeyHandler: EventHandlerRef?
    private var keyDown = false
    private var escapeKeyDownConsumed = false
    private var session = UUID()
    private var audioOutput: SystemAudioOutput.Snapshot?
    private var wasMuted: Bool?
    private var audioRestoreTimer: Timer?
    private var overlay: RecordingOverlay?
    private var overlayTimer: Timer?

    func applicationDidFinishLaunching(_ notification: Notification) {
        DebugLog.shared.setRetention(hours: settings.logRetentionHours)
        DebugLog.shared.record("app start shortcut=\(settings.shortcut) mode=\(settings.recordingControl)")
        NSApp.setActivationPolicy(.accessory)
        setupMenu()
        installEventTap()
        installEscapeMonitor()
        installEscapeHotKey()
    }

    func applicationWillTerminate(_ notification: Notification) {
        DebugLog.shared.record("app terminate; cancelling and restoring audio")
        if let escapeMonitor {
            NSEvent.removeMonitor(escapeMonitor)
            self.escapeMonitor = nil
        }
        unregisterEscapeHotKeys()
        if let escapeHotKeyHandler {
            RemoveEventHandler(escapeHotKeyHandler)
            self.escapeHotKeyHandler = nil
        }
        discardRecording()
        cancelTranscriptions()
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
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "설정…", action: #selector(showSettings(_:)), keyEquivalent: ","))
        menu.addItem(NSMenuItem(title: "로그 및 녹음 원본 폴더", action: #selector(openLog(_:)), keyEquivalent: ""))
        menu.addItem(.separator())
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "개발 버전"
        let versionItem = NSMenuItem(title: "버전 \(version)", action: nil, keyEquivalent: "")
        versionItem.isEnabled = false
        menu.addItem(versionItem)
        menu.addItem(NSMenuItem(title: "재실행", action: #selector(restart(_:)), keyEquivalent: ""))
        menu.addItem(NSMenuItem(title: "종료", action: #selector(quit(_:)), keyEquivalent: "q"))
        for item in menu.items where item.action != nil { item.target = self }
        statusItem.menu = menu
    }

    private func setStatus(_ message: String) {
        statusLine.title = message
    }

    /// 메뉴 막대 아이콘을 현재 상태에 맞춰 물들인다. 녹음 위젯을 "표시 안 함"으로
    /// 둔 사용자에게는 이 색이 녹음 중인지 알 수 있는 유일한 단서다.
    private func updateStatusIcon() {
        guard let button = statusItem?.button else { return }
        switch phase {
        case .idle: button.contentTintColor = nil
        case .recording: button.contentTintColor = .systemRed
        case .transcribing: button.contentTintColor = .systemOrange
        }
    }

    private func installEventTap() {
        let mask = (CGEventMask(1) << CGEventType.flagsChanged.rawValue)
            | (CGEventMask(1) << CGEventType.keyDown.rawValue)
            | (CGEventMask(1) << CGEventType.keyUp.rawValue)
        let context = Unmanaged.passUnretained(self).toOpaque()
        guard let tap = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .headInsertEventTap,
                                          options: .defaultTap, eventsOfInterest: mask,
                                          callback: eventTapCallback, userInfo: context) else {
            DebugLog.shared.record("event tap creation failed")
            let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
            _ = AXIsProcessTrustedWithOptions(options)
            setStatus("손쉬운 사용 권한을 허용한 뒤 앱을 다시 실행해 주세요")
            return
        }
        eventTap = tap
        let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
        DebugLog.shared.record("event tap enabled")
    }

    // Some focused applications do not forward Escape to the session event tap,
    // even though modifier changes for the recording shortcut still arrive.
    // Keep a dedicated global monitor as a fallback so the visible recording
    // overlay can always be cancelled.
    private func installEscapeMonitor() {
        escapeMonitor = NSEvent.addGlobalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard event.keyCode == 53 else { return }
            DispatchQueue.main.async {
                guard let self, self.phase != .idle else { return }
                DebugLog.shared.record("escape received by global monitor; cancelling recording/transcription")
                self.cancelRecording()
            }
        }
        DebugLog.shared.record("global escape monitor \(escapeMonitor == nil ? "unavailable" : "enabled")")
    }

    // Registering Escape as a hot key provides a separate delivery path from
    // event taps and event monitors, including for keyboard remappers.
    private func installEscapeHotKey() {
        var eventType = EventTypeSpec(eventClass: OSType(kEventClassKeyboard),
                                      eventKind: UInt32(kEventHotKeyPressed))
        let context = Unmanaged.passUnretained(self).toOpaque()
        let handlerStatus = InstallEventHandler(GetApplicationEventTarget(), { _, _, userInfo in
            guard let userInfo else { return noErr }
            let app = Unmanaged<KeyScribeApp>.fromOpaque(userInfo).takeUnretainedValue()
            DispatchQueue.main.async {
                guard app.phase != .idle else { return }
                DebugLog.shared.record("escape received by global hot key; cancelling recording/transcription")
                app.cancelRecording()
            }
            return noErr
        }, 1, &eventType, context, &escapeHotKeyHandler)
        if handlerStatus != noErr {
            DebugLog.shared.record("global escape hot key handler failed status=\(handlerStatus)")
            escapeHotKeyHandler = nil
        }
    }

    // A registered hot key swallows Escape for every other application, so it
    // must only exist while a recording or a transcription is cancellable.
    private func registerEscapeHotKeys() {
        guard escapeHotKeyHandler != nil, escapeHotKeys.isEmpty else { return }
        // A hold-to-record shortcut is commonly still held when Escape is
        // pressed. Carbon hot keys match modifiers exactly, so register every
        // combination of the standard modifier keys as an Escape cancellation.
        let modifierSets: [UInt32] = (0..<16).map { mask in
            var modifiers: UInt32 = 0
            if mask & 1 != 0 { modifiers |= UInt32(cmdKey) }
            if mask & 2 != 0 { modifiers |= UInt32(optionKey) }
            if mask & 4 != 0 { modifiers |= UInt32(controlKey) }
            if mask & 8 != 0 { modifiers |= UInt32(shiftKey) }
            return modifiers
        }
        for (index, modifiers) in modifierSets.enumerated() {
            let hotKeyID = EventHotKeyID(signature: OSType(0x4B534553), id: UInt32(index + 1)) // "KSES"
            var hotKey: EventHotKeyRef?
            let status = RegisterEventHotKey(UInt32(kVK_Escape), modifiers, hotKeyID,
                                              GetApplicationEventTarget(), 0, &hotKey)
            if status == noErr, let hotKey {
                escapeHotKeys.append(hotKey)
            } else {
                DebugLog.shared.record("global escape hot key registration failed modifiers=0x\(String(modifiers, radix: 16)) status=\(status)")
            }
        }
        DebugLog.shared.record("global escape hot keys registered count=\(escapeHotKeys.count)")
    }

    private func unregisterEscapeHotKeys() {
        guard !escapeHotKeys.isEmpty else { return }
        escapeHotKeys.forEach { _ = UnregisterEventHotKey($0) }
        escapeHotKeys = []
        DebugLog.shared.record("global escape hot keys unregistered")
    }

    // Escape belongs to KeyScribe alone while the overlay is up: cancelling a
    // recording or a transcription must not also close a sheet or drop an
    // editor out of insert mode in the focused application. Consuming the key
    // down keeps it from ever reaching another process, and the matching key up
    // follows it so nothing sees an unpaired release.
    func consumesEscape(type: CGEventType, code: CGKeyCode) -> Bool {
        guard code == 53 else { return false }
        switch type {
        case .keyDown:
            escapeKeyDownConsumed = phase != .idle
            return escapeKeyDownConsumed
        case .keyUp where escapeKeyDownConsumed:
            escapeKeyDownConsumed = false
            return true
        default:
            return false
        }
    }

    func enableEventTap() {
        if let eventTap { CGEvent.tapEnable(tap: eventTap, enable: true) }
        DebugLog.shared.record("event tap reenabled")
    }

    func handleKeyEvent(type: CGEventType, code: CGKeyCode, flags: CGEventFlags) {
        if code == triggerKeyCode() || code == 53 || code == 57 || code == 61 {
            DebugLog.shared.record("key code=\(code) type=\(type.rawValue) flags=0x\(String(flags.rawValue, radix: 16)) trigger=\(triggerKeyCode()) phase=\(phase) keyDown=\(keyDown)")
        }
        if type == .keyDown && code == 53 {
            DebugLog.shared.record(phase == .idle ? "escape ignored: idle" : "escape cancels recording/transcription")
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
        if pressed == keyDown {
            DebugLog.shared.record("trigger duplicate state ignored pressed=\(pressed)")
            return
        }
        keyDown = pressed
        DebugLog.shared.record("trigger state pressed=\(pressed) mode=\(settings.recordingControl) phase=\(phase)")
        if pressed {
            // 앞선 녹음을 변환하는 중이어도 새 녹음은 바로 받는다.
            if phase != .recording { startRecording() }
            else if settings.recordingControl == "toggle" { stopRecording() }
        } else if phase == .recording && settings.recordingControl == "hold" {
            stopRecording()
        }
    }

    private func triggerKeyCode() -> CGKeyCode {
        keyCodes[settings.shortcut] ?? 54
    }

    private func startRecording() {
        DebugLog.shared.record("recording start requested")
        guard recordingLimitSound?.isPlaying != true else {
            setStatus("종료음 재생 중")
            return
        }
        guard !settings.apiKey.isEmpty else {
            setStatus("API 설정 필요")
            showTransientOverlay(.apiSetupRequired)
            return
        }
        let authorization = AVCaptureDevice.authorizationStatus(for: .audio)
        if authorization == .notDetermined {
            AVCaptureDevice.requestAccess(for: .audio) { [weak self] granted in
                DispatchQueue.main.async {
                    guard let self else { return }
                    if granted {
                        guard self.phase != .recording else { return }
                        if self.settings.recordingControl == "toggle" || self.keyDown { self.startRecording() }
                    } else {
                        DebugLog.shared.record("microphone permission denied")
                        self.setStatus("마이크 권한이 필요합니다")
                        self.showTransientOverlay(.microphoneError)
                    }
                }
            }
            return
        }
        guard authorization == .authorized else {
            DebugLog.shared.record("microphone permission unavailable status=\(authorization.rawValue)")
            setStatus("마이크 권한이 필요합니다")
            showTransientOverlay(.microphoneError)
            return
        }
        session = UUID()
        let url = DebugLog.shared.newRecordingURL()
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
            refreshPhase()
            recordingStartedAt = ProcessInfo.processInfo.systemUptime
            recordingLimitMinutes = settings.recordingTimeLimitMinutes
            recordingSecondsShown = nil
            let currentSession = session
            let timer = Timer(timeInterval: TimeInterval(recordingLimitMinutes * 60),
                              repeats: false) { [weak self] _ in
                guard let self, self.phase == .recording, self.session == currentSession else { return }
                DebugLog.shared.record("recording time limit reached")
                self.stopRecording(limitReached: true)
                self.playRecordingLimitSound()
            }
            recordingLimitTimer = timer
            RunLoop.main.add(timer, forMode: .common)
            DebugLog.shared.record("recording started")
            showActiveOverlay(.recording)
            updateRecordingElapsed()
            if settings.recordingStartSoundVolume > 0,
               let sound = makeRecordingStartSound(volume: settings.recordingStartSoundVolume) {
                recordingStartSound = sound
                sound.prepareToPlay()
                if sound.play() {
                    if settings.muteDuringRecording {
                        let currentSession = session
                        DispatchQueue.main.asyncAfter(deadline: .now() + sound.duration + 0.03) { [weak self] in
                            guard let self, self.phase == .recording, self.session == currentSession else { return }
                            self.muteSystemAudio()
                        }
                    }
                } else if settings.muteDuringRecording {
                    muteSystemAudio()
                }
            } else if settings.muteDuringRecording {
                muteSystemAudio()
            }
        } catch {
            DebugLog.shared.record("recording start failed type=\(type(of: error))")
            recorder = nil
            try? FileManager.default.removeItem(at: url)
            setStatus("녹음 오류: \(error.localizedDescription)")
            showTransientOverlay(.microphoneError)
        }
    }

    private func makeRecordingStartSound(volume: Int) -> AVAudioPlayer? {
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

    private func playRecordingLimitSound() {
        guard let url = Bundle.main.url(forResource: "recording-limit", withExtension: "wav"),
              let sound = try? AVAudioPlayer(contentsOf: url) else { return }
        recordingLimitSound = sound
        sound.prepareToPlay()
        sound.play()
    }

    private func stopRecording(limitReached: Bool = false) {
        guard phase == .recording, let url = recordingURL else { return }
        recordingLimitTimer?.invalidate()
        recordingLimitTimer = nil
        recordingStartedAt = nil
        recordingSecondsShown = nil
        DebugLog.shared.record("recording stop requested")
        if let sound = recordingStartSound {
            let remaining = max(0, sound.duration - sound.currentTime)
            DispatchQueue.main.asyncAfter(deadline: .now() + remaining + 0.02) { sound.stop() }
        }
        recordingStartSound = nil
        recorder?.stop()
        recorder = nil
        restoreSystemAudio()
        DebugLog.shared.record("recording stopped; audio restore attempted; saved=\(url.lastPathComponent)")
        recordingURL = nil
        let job = TranscriptionJob()
        jobs.append(job)
        refreshPhase()
        setStatus(limitReached ? "시간 제한 도달 · 변환 중" : transcribingStatus())
        showActiveOverlay(.transcribing)
        job.transcriber.transcribe(audioURL: url, settings: settings) { [weak self, weak job] result in
            DispatchQueue.main.async {
                guard let self, let job else { return }
                self.complete(job, with: result)
            }
        }
    }

    private func refreshPhase() {
        phase = recordingURL != nil ? .recording : jobs.isEmpty ? .idle : .transcribing
    }

    private func transcribingStatus() -> String {
        jobs.count > 1 ? "변환 중 (\(jobs.count)건) · Esc 취소" : "변환 중 · Esc 취소"
    }

    private func complete(_ job: TranscriptionJob, with result: Result<String, Error>) {
        // 취소된 작업은 목록에서 이미 빠져 있다.
        guard jobs.contains(where: { $0 === job }) else { return }
        job.result = result
        // 먼저 녹음한 것이 끝나기 전까지는 뒤의 결과를 붙여넣지 않고 기다린다.
        while let first = jobs.first, let result = first.result {
            jobs.removeFirst()
            deliver(result)
        }
        refreshPhase()
        switch phase {
        case .recording:
            break
        case .transcribing:
            setStatus(transcribingStatus())
        case .idle:
            if !showPendingFailure() { hideOverlay() }
        }
    }

    /// 녹음 중이거나 앞선 결과를 기다리느라 미뤄 둔 실패를 이제 알린다.
    private func showPendingFailure() -> Bool {
        guard let failure = pendingFailure else { return false }
        pendingFailure = nil
        setStatus(failure)
        showTransientOverlay(.failed)
        return true
    }

    private func deliver(_ result: Result<String, Error>) {
        switch result {
        case .success(let transcript):
            DebugLog.shared.record("transcription completed characters=\(transcript.count)")
            let text = settings.applyingReplacements(to: transcript)
            guard !text.isEmpty else { setStatus("인식된 음성이 없습니다"); return }
            paste(text)
            setStatus("완료")
        case .failure(let error):
            DebugLog.shared.record("transcription failed type=\(type(of: error))")
            pendingFailure = error.localizedDescription
        }
    }

    /// Esc는 지금 위젯에 보이는 것을 취소한다. 녹음 중이면 그 녹음만 버리고
    /// 앞서 끝낸 녹음의 변환은 이어 간다. 변환만 남았다면 변환을 모두 취소한다.
    private func cancelRecording() {
        DebugLog.shared.record("recording/transcription cancelled phase=\(phase) pending=\(jobs.count)")
        if phase == .recording {
            discardRecording()
        } else {
            cancelTranscriptions()
        }
        refreshPhase()
        if phase == .transcribing {
            setStatus("녹음 취소됨 · " + transcribingStatus())
            showActiveOverlay(.transcribing)
            return
        }
        if showPendingFailure() { return }
        if statusLine != nil { setStatus("취소됨") }
        showTransientOverlay(.cancelled)
    }

    private func discardRecording() {
        session = UUID()
        recordingLimitTimer?.invalidate()
        recordingLimitTimer = nil
        recordingLimitSound?.stop()
        recordingLimitSound = nil
        recordingStartedAt = nil
        recordingSecondsShown = nil
        recordingStartSound?.stop()
        recordingStartSound = nil
        recorder?.stop()
        recorder = nil
        // 녹음 중 취소는 버리지만, 녹음을 끝낸 원본은 전사를 취소해도 남긴다.
        if let recordingURL { try? FileManager.default.removeItem(at: recordingURL) }
        recordingURL = nil
        restoreSystemAudio()
    }

    private func cancelTranscriptions() {
        jobs.forEach { $0.transcriber.cancel() }
        jobs = []
        pendingFailure = nil
    }

    private func showActiveOverlay(_ state: RecordingOverlay.State) {
        // 위젯을 꺼 두었어도 타이머는 계속 돌려서 메뉴 막대의 경과 시간을 갱신한다.
        if overlayPosition.showsWidget {
            if overlay == nil { overlay = RecordingOverlay() }
            overlay?.show(state, position: overlayPosition)
        } else {
            overlay?.hide()
        }
        overlayTimer?.invalidate()
        let timer = Timer(timeInterval: 0.05, repeats: true) { [weak self] _ in
            guard let self else { return }
            let level: CGFloat?
            if self.phase == .recording, let recorder = self.recorder {
                self.updateRecordingElapsed()
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

    private func updateRecordingElapsed() {
        guard phase == .recording, let started = recordingStartedAt else { return }
        let seconds = max(0, Int(ProcessInfo.processInfo.systemUptime - started))
        guard recordingSecondsShown != seconds else { return }
        recordingSecondsShown = seconds
        let elapsed = String(format: "%02d:%02d", seconds / 60, seconds % 60)
        setStatus("녹음 중 (\(elapsed)) · Esc 취소")
        overlay?.updateRecordingTime(seconds, warning: seconds >= recordingLimitMinutes * 60 - 60)
    }

    private func showTransientOverlay(_ state: RecordingOverlay.State) {
        overlayTimer?.invalidate()
        overlayTimer = nil
        guard overlayPosition.showsWidget else {
            overlay?.hide()
            return
        }
        if overlay == nil { overlay = RecordingOverlay() }
        overlay?.show(state, position: overlayPosition)
        let currentSession = session
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { [weak self] in
            guard let self, self.session == currentSession, self.phase == .idle else { return }
            self.overlay?.hide()
        }
    }

    private var overlayPosition: OverlayPosition {
        OverlayPosition(rawValue: settings.overlayPosition) ?? .bottomCenter
    }

    private func hideOverlay() {
        overlayTimer?.invalidate()
        overlayTimer = nil
        overlay?.hide()
    }

    /// 결과가 연달아 나와도 앞선 붙여넣기가 끝난 뒤에 이어 붙이도록 줄을 세운다.
    private func paste(_ text: String) {
        pasteQueue += KeyToken.segments(of: text)
        // 자동 Enter도 마지막 키 조각일 뿐이라, 키 토큰과 같은 경로로 내보낸다.
        if settings.autoSend { pasteQueue.append(.key(flags: [], code: 36)) }
        if !pasteScheduled { runPaste() }
    }

    /// 조각을 하나씩 내보낸다. 붙여넣기를 비동기로 반영하는 편집기가 있어
    /// 텍스트 뒤에는 조금 더 오래 기다렸다가 다음 조각으로 넘어간다. 마지막
    /// 조각 뒤에도 기다려야 다음 결과가 클립보드를 너무 일찍 덮어쓰지 않는다.
    private func runPaste() {
        pasteScheduled = false
        guard !pasteQueue.isEmpty else { return }
        // 녹음 단축키를 누르고 있는 동안 키를 보내면 그 수식키가 섞여 들어간다.
        guard !keyDown else { schedulePaste(after: 0.05); return }
        guard let source = CGEventSource(stateID: .hidSystemState) else {
            DebugLog.shared.record("paste dropped: event source unavailable")
            pasteQueue = []
            return
        }
        let segment = pasteQueue.removeFirst()
        switch segment {
        case .text(let value):
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(value, forType: .string)
            postKey(source: source, code: 9, flags: .maskCommand, hold: 0)
            schedulePaste(after: 0.4)
        case .key(let flags, let code):
            postKey(source: source, code: code, flags: flags, hold: 0.04)
            schedulePaste(after: 0.12)
        }
    }

    private func schedulePaste(after delay: TimeInterval) {
        pasteScheduled = true
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            self?.runPaste()
        }
    }

    private func postKey(source: CGEventSource, code: CGKeyCode, flags: CGEventFlags,
                         hold: TimeInterval) {
        guard let down = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: true),
              let up = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: false) else { return }
        down.flags = flags
        up.flags = flags
        down.post(tap: .cghidEventTap)
        guard hold > 0 else {
            up.post(tap: .cghidEventTap)
            return
        }
        // 물리 키를 누른 것처럼 잠깐 붙잡고 있어야 받아들이는 앱이 있다.
        DispatchQueue.main.asyncAfter(deadline: .now() + hold) { up.post(tap: .cghidEventTap) }
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
        if audioOutput != nil || wasMuted == false {
            restoreSystemAudio()
            if audioOutput != nil || wasMuted == false {
                DebugLog.shared.record("audio mute state retained while restore is pending")
                return
            }
        }
        if let output = SystemAudioOutput.capture() {
            audioOutput = output
            DebugLog.shared.record("audio output captured muted=\(output.wasMuted)")
            guard !output.wasMuted else { return }
            guard let volume = output.volume, volume > 0 else {
                if !SystemAudioOutput.setMuted(true, on: output) {
                    DebugLog.shared.record("audio mute failed")
                    audioOutput = nil
                }
                return
            }
            let currentSession = session
            for step in 1...5 {
                DispatchQueue.main.asyncAfter(deadline: .now() + Double(step) * 0.025) { [weak self] in
                    guard let self, self.phase == .recording, self.session == currentSession,
                          self.audioOutput?.device == output.device else { return }
                    let level = volume * Float(5 - step) / 5
                    SystemAudioOutput.setVolume(level, on: output)
                    if step == 5 && SystemAudioOutput.setMuted(true, on: output) {
                        SystemAudioOutput.setVolume(volume, on: output)
                    } else if step == 5 {
                        DebugLog.shared.record("audio mute failed after fade")
                    }
                }
            }
            return
        }
        guard let state = runAppleScript("output muted of (get volume settings)") else {
            DebugLog.shared.record("audio mute skipped: state query failed")
            return
        }
        wasMuted = state == "true"
        if wasMuted == false {
            if runAppleScript("set volume output muted true") == nil {
                DebugLog.shared.record("audio mute failed")
                wasMuted = nil
            } else {
                DebugLog.shared.record("audio muted; previous state=unmuted")
            }
        } else {
            DebugLog.shared.record("audio already muted; preserving user state")
        }
    }

    private func restoreSystemAudio() {
        if let output = audioOutput {
            if let volume = output.volume { SystemAudioOutput.setVolume(volume, on: output) }
            if !output.wasMuted {
                for attempt in 1...3 {
                    if SystemAudioOutput.setMuted(false, on: output) {
                        DebugLog.shared.record("audio restored attempt=\(attempt)")
                        audioRestoreTimer?.invalidate()
                        audioRestoreTimer = nil
                        audioOutput = nil
                        return
                    }
                    DebugLog.shared.record("audio restore failed attempt=\(attempt)")
                    if attempt < 3 { Thread.sleep(forTimeInterval: 0.2) }
                }
                scheduleAudioRestore()
                return
            }
            audioOutput = nil
        }
        if wasMuted == false {
            for attempt in 1...3 {
                if runAppleScript("set volume output muted false") != nil {
                    DebugLog.shared.record("audio restored attempt=\(attempt)")
                    audioRestoreTimer?.invalidate()
                    audioRestoreTimer = nil
                    wasMuted = nil
                    return
                }
                DebugLog.shared.record("audio restore failed attempt=\(attempt)")
                if attempt < 3 { Thread.sleep(forTimeInterval: 0.2) }
            }
            scheduleAudioRestore()
            return
        }
        wasMuted = nil
    }

    private func scheduleAudioRestore() {
        if audioRestoreTimer == nil {
            audioRestoreTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
                self?.restoreSystemAudio()
            }
        }
    }

    @objc private func openLog(_ sender: Any?) {
        DebugLog.shared.record("log folder opened by user")
        NSWorkspace.shared.open(DebugLog.shared.directory)
    }

    @objc private func restart(_ sender: Any?) {
        // The development LaunchAgent has KeepAlive enabled. Starting a second
        // process here would leave two keyboard hooks alive, so let it perform
        // the relaunch when it owns this process.
        if ProcessInfo.processInfo.environment["XPC_SERVICE_NAME"] == "net.gitools.keyscribe.dev" {
            DebugLog.shared.record("app restart requested through LaunchAgent")
            NSApp.terminate(nil)
            return
        }
        guard let executable = ProcessInfo.processInfo.arguments.first, !executable.isEmpty else {
            setStatus("재실행할 실행 파일을 찾을 수 없습니다")
            return
        }
        let relauncher = Process()
        relauncher.executableURL = URL(fileURLWithPath: "/bin/sh")
        // Relaunch after this instance releases its global keyboard hooks.
        relauncher.arguments = ["-c", "sleep 0.3; exec \"$1\"", "--", executable]
        do {
            try relauncher.run()
            DebugLog.shared.record("app restart requested")
            NSApp.terminate(nil)
        } catch {
            DebugLog.shared.record("app restart failed type=\(type(of: error))")
            setStatus("재실행 실패: \(error.localizedDescription)")
        }
    }

    @objc private func quit(_ sender: Any?) { NSApp.terminate(nil) }

    @objc private func showSettings(_ sender: Any?) {
        let dialog = SettingsDialog(settings: settings)
        guard let updated = dialog.present() else { return }
        do {
            try updated.save()
            settings = updated
            DebugLog.shared.setRetention(hours: updated.logRetentionHours)
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
