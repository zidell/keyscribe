import AppKit
import CoreGraphics

/// 붙여넣을 내용을 텍스트 덩어리와 키 입력으로 나눈 조각.
/// 치환 단어의 바꿀 말에 `[enter]`, `[cmd+k]`처럼 적으면 그 자리에서 실제 키를 누른다.
enum PasteSegment: Equatable {
    case text(String)
    case key(flags: CGEventFlags, code: CGKeyCode)
}

enum KeyToken {
    private static let modifiers: [String: CGEventFlags] = [
        "cmd": .maskCommand, "command": .maskCommand, "meta": .maskCommand,
        "super": .maskCommand, "win": .maskCommand, "⌘": .maskCommand,
        "ctrl": .maskControl, "control": .maskControl, "⌃": .maskControl,
        "alt": .maskAlternate, "option": .maskAlternate, "opt": .maskAlternate, "⌥": .maskAlternate,
        "shift": .maskShift, "⇧": .maskShift,
    ]

    private static let keys: [String: CGKeyCode] = {
        var table: [String: CGKeyCode] = [
            "enter": 36, "return": 36, "tab": 48, "space": 49, "spacebar": 49,
            "esc": 53, "escape": 53,
            // 맥 키보드에 Delete라고 적힌 키는 지우고 뒤로 가는 키다.
            "backspace": 51, "delete": 51, "del": 51,
            "forwarddelete": 117, "fndelete": 117,
            "up": 126, "down": 125, "left": 123, "right": 124,
            "arrowup": 126, "arrowdown": 125, "arrowleft": 123, "arrowright": 124,
            "home": 115, "end": 119,
            "pageup": 116, "pgup": 116, "pagedown": 121, "pgdn": 121, "pgdown": 121,
            "f1": 122, "f2": 120, "f3": 99, "f4": 118,
            "f5": 96, "f6": 97, "f7": 98, "f8": 100,
            "f9": 101, "f10": 109, "f11": 103, "f12": 111,
        ]
        let letters: [CGKeyCode] = [0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46,
                                    45, 31, 35, 12, 15, 1, 17, 32, 9, 13, 7, 16, 6]
        for (index, letter) in "abcdefghijklmnopqrstuvwxyz".enumerated() {
            table[String(letter)] = letters[index]
        }
        let digits: [CGKeyCode] = [29, 18, 19, 20, 21, 23, 22, 26, 28, 25]
        for (index, digit) in "0123456789".enumerated() {
            table[String(digit)] = digits[index]
        }
        return table
    }()

    /// 사용자가 고를 수 있는 키 이름을 사용법 안내에 쓰기 좋게 정리한 목록.
    static let keyNames = "enter, tab, space, esc, backspace, delete, up, down, left, right, "
        + "home, end, pageup, pagedown, f1~f12, a~z, 0~9"

    /// 대괄호 안쪽 내용을 키 조합으로 해석한다. 대소문자와 공백은 따지지 않고,
    /// 연결 기호는 `+`든 `-`든 받는다. `[Cmd + K]`, `[cmd-k]`, `[Page Up]` 모두 같은 뜻이다.
    /// 모르는 이름이 하나라도 있으면 nil을 돌려주고, 그러면 대괄호는 그냥 글자로 남는다.
    static func parse(_ body: String) -> (flags: CGEventFlags, code: CGKeyCode)? {
        let normalized = body.lowercased().filter { !$0.isWhitespace }
        var flags: CGEventFlags = []
        var code: CGKeyCode?
        var needsModifier = false
        for part in normalized.split(whereSeparator: { $0 == "+" || $0 == "-" }) {
            let name = String(part)
            if let modifier = modifiers[name] {
                flags.insert(modifier)
                continue
            }
            guard code == nil, let key = keys[name] else { return nil }
            code = key
            // 전사문에 그냥 들어온 `[0]`, `[a]` 같은 대괄호를 키로 오해하지 않도록
            // 글자·숫자 한 글자는 조합키와 같이 쓸 때만 키로 본다.
            needsModifier = name.count == 1
        }
        guard let code, !(needsModifier && flags.isEmpty) else { return nil }
        return (flags, code)
    }

    /// 붙여넣을 문자열을 텍스트 조각과 키 조각으로 나눈다.
    static func segments(of text: String) -> [PasteSegment] {
        var segments: [PasteSegment] = []
        var buffer = ""
        var index = text.startIndex
        while index < text.endIndex {
            let character = text[index]
            let next = text.index(after: index)
            guard character == "[", let close = text[next...].firstIndex(of: "]"),
                  let key = parse(String(text[next..<close])) else {
                buffer.append(character)
                index = next
                continue
            }
            if !buffer.isEmpty {
                segments.append(.text(buffer))
                buffer = ""
            }
            segments.append(.key(flags: key.flags, code: key.code))
            index = text.index(after: close)
        }
        if !buffer.isEmpty { segments.append(.text(buffer)) }
        return segments
    }
}
