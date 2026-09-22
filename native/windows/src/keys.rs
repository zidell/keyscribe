use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;

/// 붙여넣을 내용을 텍스트 덩어리와 키 입력으로 나눈 조각.
/// 치환 단어의 바꿀 말에 `[enter]`, `[ctrl+k]`처럼 적으면 그 자리에서 실제 키를 누른다.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Segment {
    Text(String),
    /// 누를 순서(보조키 먼저)와 뗄 순서(키 먼저)를 미리 담아 둔다.
    Key {
        down: Vec<u16>,
        up: Vec<u16>,
    },
}

/// 사용법 안내에 쓰는 키 이름 목록.
pub const KEY_NAMES: &str = "enter, tab, space, esc, backspace, delete, up, down, left, right, \
                             home, end, pageup, pagedown, f1~f12, a~z, 0~9";

fn modifier_code(name: &str) -> Option<u16> {
    Some(match name {
        // 윈도우에는 Command 키가 없다. 맥 습관대로 적은 cmd는 Ctrl로 받아 준다.
        "ctrl" | "control" | "cmd" | "command" => VK_CONTROL,
        "alt" | "option" | "opt" => VK_MENU,
        "shift" => VK_SHIFT,
        "win" | "super" | "meta" => VK_LWIN,
        _ => return None,
    })
}

fn key_code(name: &str) -> Option<u16> {
    let named = match name {
        "enter" | "return" => Some(VK_RETURN),
        "tab" => Some(VK_TAB),
        "space" | "spacebar" => Some(VK_SPACE),
        "esc" | "escape" => Some(VK_ESCAPE),
        "backspace" => Some(VK_BACK),
        "delete" | "del" => Some(VK_DELETE),
        "up" | "arrowup" => Some(VK_UP),
        "down" | "arrowdown" => Some(VK_DOWN),
        "left" | "arrowleft" => Some(VK_LEFT),
        "right" | "arrowright" => Some(VK_RIGHT),
        "home" => Some(VK_HOME),
        "end" => Some(VK_END),
        "pageup" | "pgup" => Some(VK_PRIOR),
        "pagedown" | "pgdn" | "pgdown" => Some(VK_NEXT),
        "f1" => Some(VK_F1),
        "f2" => Some(VK_F2),
        "f3" => Some(VK_F3),
        "f4" => Some(VK_F4),
        "f5" => Some(VK_F5),
        "f6" => Some(VK_F6),
        "f7" => Some(VK_F7),
        "f8" => Some(VK_F8),
        "f9" => Some(VK_F9),
        "f10" => Some(VK_F10),
        "f11" => Some(VK_F11),
        "f12" => Some(VK_F12),
        _ => None,
    };
    if named.is_some() {
        return named;
    }
    let mut characters = name.chars();
    let single = characters.next()?;
    if characters.next().is_some() {
        return None;
    }
    if single.is_ascii_alphabetic() {
        Some(single.to_ascii_uppercase() as u16)
    } else if single.is_ascii_digit() {
        Some(single as u16)
    } else {
        None
    }
}

/// 대괄호 안쪽 내용을 키 조합으로 해석한다. 대소문자와 공백은 따지지 않고,
/// 연결 기호는 `+`든 `-`든 받는다. `[Ctrl + K]`, `[ctrl-k]`, `[Page Up]` 모두 같은 뜻이다.
/// 모르는 이름이 하나라도 있으면 None을 돌려주고, 그러면 대괄호는 그냥 글자로 남는다.
fn parse_token(body: &str) -> Option<(Vec<u16>, u16)> {
    let normalized: String = body
        .chars()
        .filter(|character| !character.is_whitespace())
        .collect::<String>()
        .to_lowercase();
    let mut modifiers: Vec<u16> = Vec::new();
    let mut key = None;
    let mut needs_modifier = false;
    for part in normalized.split(['+', '-']).filter(|part| !part.is_empty()) {
        if let Some(modifier) = modifier_code(part) {
            if !modifiers.contains(&modifier) {
                modifiers.push(modifier);
            }
            continue;
        }
        if key.is_some() {
            return None;
        }
        key = Some(key_code(part)?);
        // 전사문에 그냥 들어온 `[0]`, `[a]` 같은 대괄호를 키로 오해하지 않도록
        // 글자·숫자 한 글자는 조합키와 같이 쓸 때만 키로 본다.
        needs_modifier = part.chars().count() == 1;
    }
    let key = key?;
    if needs_modifier && modifiers.is_empty() {
        return None;
    }
    Some((modifiers, key))
}

/// 붙여넣을 문자열을 텍스트 조각과 키 조각으로 나눈다.
pub fn segments(text: &str) -> Vec<Segment> {
    let mut result = Vec::new();
    let mut buffer = String::new();
    let mut rest = text;
    while let Some(open) = rest.find('[') {
        let after = &rest[open + 1..];
        let Some(close) = after.find(']') else { break };
        let Some((modifiers, key)) = parse_token(&after[..close]) else {
            buffer.push_str(&rest[..=open]);
            rest = after;
            continue;
        };
        buffer.push_str(&rest[..open]);
        if !buffer.is_empty() {
            result.push(Segment::Text(std::mem::take(&mut buffer)));
        }
        let mut down = modifiers.clone();
        down.push(key);
        let mut up = vec![key];
        up.extend(modifiers.into_iter().rev());
        result.push(Segment::Key { down, up });
        rest = &after[close + 1..];
    }
    buffer.push_str(rest);
    if !buffer.is_empty() {
        result.push(Segment::Text(buffer));
    }
    result
}

#[cfg(test)]
mod tests {
    use super::{segments, Segment};
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::*;

    fn text(value: &str) -> Segment {
        Segment::Text(value.into())
    }

    fn key(down: Vec<u16>, up: Vec<u16>) -> Segment {
        Segment::Key { down, up }
    }

    #[test]
    fn splits_text_around_key_tokens() {
        assert_eq!(
            segments("첫째[enter]둘째"),
            vec![
                text("첫째"),
                key(vec![VK_RETURN], vec![VK_RETURN]),
                text("둘째"),
            ]
        );
    }

    #[test]
    fn ignores_case_spacing_and_the_join_character() {
        let expected = key(vec![VK_CONTROL, b'K' as u16], vec![b'K' as u16, VK_CONTROL]);
        // 맥 습관대로 적은 cmd도 Ctrl로 받아 준다.
        for token in [
            "[ctrl+k]",
            "[Ctrl + K]",
            "[CTRL-K]",
            "[ ctrl + K ]",
            "[cmd+k]",
        ] {
            assert_eq!(segments(token), vec![expected.clone()], "{token}");
        }
        assert_eq!(
            segments("[Page Up]"),
            vec![key(vec![VK_PRIOR], vec![VK_PRIOR])]
        );
    }

    #[test]
    fn releases_modifiers_in_reverse_order() {
        assert_eq!(
            segments("[ctrl+shift+p]"),
            vec![key(
                vec![VK_CONTROL, VK_SHIFT, b'P' as u16],
                vec![b'P' as u16, VK_SHIFT, VK_CONTROL],
            )]
        );
    }

    /// 전사문에 그대로 들어온 대괄호는 키로 보지 않고 글자로 남겨야 한다.
    #[test]
    fn leaves_unknown_and_bare_single_character_brackets_alone() {
        for literal in [
            "[BLANK_AUDIO] 안녕하세요",
            "배열은 [0]번부터 시작",
            "각주 [1] 참고",
            "[k]",
            "[]",
            "닫는 괄호 없음 [enter",
            "[cmd+]",
        ] {
            assert_eq!(segments(literal), vec![text(literal)], "{literal}");
        }
    }

    #[test]
    fn a_single_character_key_works_with_a_modifier() {
        assert_eq!(
            segments("[cmd+0]"),
            vec![key(
                vec![VK_CONTROL, b'0' as u16],
                vec![b'0' as u16, VK_CONTROL],
            )]
        );
    }
}
