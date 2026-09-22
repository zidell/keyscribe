import AppKit
import QuartzCore

enum OverlayPosition: String, CaseIterable {
    case hidden = "hidden"
    case topLeft = "top_left"
    case topCenter = "top_center"
    case topRight = "top_right"
    case center = "center"
    case bottomLeft = "bottom_left"
    case bottomCenter = "bottom_center"
    case bottomRight = "bottom_right"

    var title: String {
        switch self {
        case .hidden: return "표시 안 함"
        case .topLeft: return "상단 왼쪽"
        case .topCenter: return "상단 중앙"
        case .topRight: return "상단 오른쪽"
        case .center: return "정중앙"
        case .bottomLeft: return "하단 왼쪽"
        case .bottomCenter: return "하단 중앙"
        case .bottomRight: return "하단 오른쪽"
        }
    }

    /// 위젯을 아예 띄우지 않는 설정인지 여부.
    var showsWidget: Bool { self != .hidden }

    /// 화면 여백(visibleFrame) 안에서 주어진 크기의 창이 놓일 왼쪽 아래 좌표.
    func origin(in frame: NSRect, size: NSSize, margin: CGFloat = 40) -> NSPoint {
        let x: CGFloat
        switch self {
        case .topLeft, .bottomLeft: x = frame.minX + margin
        case .hidden, .topCenter, .center, .bottomCenter: x = frame.midX - size.width / 2
        case .topRight, .bottomRight: x = frame.maxX - size.width - margin
        }
        let y: CGFloat
        switch self {
        case .topLeft, .topCenter, .topRight: y = frame.maxY - size.height - margin
        case .hidden, .center: y = frame.midY - size.height / 2
        case .bottomLeft, .bottomCenter, .bottomRight: y = frame.minY + margin
        }
        return NSPoint(x: x, y: y)
    }
}

final class RecordingOverlay {
    enum State {
        case recording
        case transcribing
        case cancelled
        case failed
        case microphoneError
        case apiSetupRequired

        var title: String {
            switch self {
            case .recording: return "🔴  녹음 중"
            case .transcribing: return "⏳  변환 중…"
            case .cancelled: return "녹음 취소됨"
            case .failed: return "변환 실패"
            case .microphoneError: return "마이크 오류"
            case .apiSetupRequired: return "API 설정 필요"
            }
        }
    }

    private let width: CGFloat = 260
    private let height: CGFloat = 52
    private let window: NSPanel
    private let label: NSTextField
    private var bars: [CALayer] = []
    private var phase: CGFloat = 0
    private var volume: CGFloat = 0
    private var state: State?

    init() {
        window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 260, height: 52),
                         styleMask: [.borderless, .nonactivatingPanel],
                         backing: .buffered, defer: false)
        window.level = .floating
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        window.isOpaque = false
        window.backgroundColor = .clear
        window.hasShadow = true
        window.ignoresMouseEvents = true

        let content = NSView(frame: NSRect(x: 0, y: 0, width: 260, height: 52))
        content.wantsLayer = true
        content.layer?.backgroundColor = NSColor(calibratedWhite: 0.1, alpha: 0.9).cgColor
        content.layer?.cornerRadius = 26
        content.layer?.masksToBounds = true
        window.contentView = content

        label = NSTextField(labelWithString: "")
        label.frame = NSRect(x: 20, y: 15, width: 165, height: 22)
        label.font = .systemFont(ofSize: 14, weight: .medium)
        label.textColor = .white
        label.alignment = .left
        content.addSubview(label)

        for index in 0..<5 {
            let bar = CALayer()
            bar.frame = NSRect(x: 210 + CGFloat(index) * 7, y: 24, width: 3, height: 4)
            bar.backgroundColor = NSColor(calibratedRed: 1, green: 0.28, blue: 0.28, alpha: 1).cgColor
            bar.cornerRadius = 1.5
            content.layer?.addSublayer(bar)
            bars.append(bar)
        }
    }

    func show(_ state: State, position: OverlayPosition = .bottomCenter) {
        self.state = state
        label.stringValue = state.title
        let pointer = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { $0.frame.contains(pointer) } ?? NSScreen.main
        if let frame = screen?.visibleFrame {
            window.setFrameOrigin(position.origin(in: frame, size: NSSize(width: width, height: height)))
        }
        window.orderFrontRegardless()
    }

    func updateRecordingTime(_ seconds: Int, warning: Bool) {
        let elapsed = String(format: "%02d:%02d", seconds / 60, seconds % 60)
        let title = "🔴  녹음 중 (\(elapsed))"
        let styled = NSMutableAttributedString(string: title, attributes: [
            .font: NSFont.systemFont(ofSize: 14, weight: .medium),
            .foregroundColor: NSColor.white,
        ])
        let timeRange = (title as NSString).range(of: "(\(elapsed))")
        styled.addAttribute(.font, value: NSFont.systemFont(ofSize: 14, weight: .regular),
                            range: timeRange)
        styled.addAttribute(.foregroundColor, value: warning
                            ? NSColor(calibratedRed: 1, green: 0.35, blue: 0.35, alpha: 1)
                            : NSColor.white.withAlphaComponent(0.5),
                            range: timeRange)
        label.attributedStringValue = styled
    }

    func tick(level: CGFloat?) {
        phase += 0.4
        if let level {
            volume = max(0, min(1, level))
        } else {
            volume *= 0.88
        }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (index, bar) in bars.enumerated() {
            let wave = 0.5 + 0.5 * sin(phase + CGFloat(index) * 1.3)
            let strength = volume * (0.5 + 0.5 * wave) + (1 - volume) * 0.12 * wave
            let barHeight = 4 + strength * 24
            bar.frame = NSRect(x: 210 + CGFloat(index) * 7,
                               y: (height - barHeight) / 2,
                               width: 3, height: barHeight)
        }
        CATransaction.commit()
    }

    func hide() {
        volume = 0
        window.orderOut(nil)
    }
}
