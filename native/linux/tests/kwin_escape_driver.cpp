#include "effect/effect.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "input.h"
#include "input_event.h"
#include "keyboard_input.h"
#include <QDBusConnection>
#include <QSet>
#include <linux/input-event-codes.h>
namespace KWin {
class EscapeDriver final : public Effect, public InputEventFilter {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.gitools.keyscribe.Driver")
public:
    EscapeDriver() : InputEventFilter(InputFilterOrder::ScreenEdge) {
        auto bus = QDBusConnection::sessionBus();
        bus.registerService("net.gitools.keyscribe.Driver");
        bus.registerObject("/net/gitools/keyscribe/Driver", this,
                           QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        input()->installInputEventFilter(this);
    }
    ~EscapeDriver() override {
        auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
        for (auto code : m_pressed) input()->keyboard()->processKey(code, KeyboardKeyState::Released, now);
        QDBusConnection::sessionBus().unregisterObject("/net/gitools/keyscribe/Driver");
        QDBusConnection::sessionBus().unregisterService("net.gitools.keyscribe.Driver");
    }
    bool isActive() const override { return false; }
    bool keyboardKey(KeyboardKeyEvent *event) override {
        if (event->nativeScanCode != KEY_F12) return false;
        if (event->state == KeyboardKeyState::Pressed) {
            input()->keyboard()->addFilteredKey(KEY_F12);
            Q_EMIT Record(true);
        } else if (event->state == KeyboardKeyState::Released) Q_EMIT Record(false);
        return true;
    }
public Q_SLOTS:
    bool Key(uint code, bool down) {
        auto window = effects->activeWindow();
        if ((!window || window->caption() != "Private Escape test" || effects->isScreenLocked()) &&
            (down || !m_pressed.contains(code))) return false;
        if (down) m_pressed.insert(code); else m_pressed.remove(code);
        auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
        input()->keyboard()->processKey(code, down ? KeyboardKeyState::Pressed : KeyboardKeyState::Released, now);
        return true;
    }
Q_SIGNALS:
    void Record(bool down);
private:
    QSet<uint> m_pressed;
};
}
KWIN_EFFECT_FACTORY(KWin::EscapeDriver, "kwin_escape_driver.json")
#include "kwin_escape_driver.moc"
