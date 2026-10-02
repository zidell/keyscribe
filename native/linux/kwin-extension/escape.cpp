#include "effect/effect.h"
#include "effect/effecthandler.h"
#include "input.h"
#include "input_event.h"
#include "keyboard_input.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QSet>

namespace KWin {
static const QString app = QStringLiteral("net.gitools.keyscribe");
static const QString path = QStringLiteral("/net/gitools/keyscribe/Escape");
static const QString iface = QStringLiteral("net.gitools.keyscribe.Escape");

class KeyScribeEscape final : public Effect, public InputEventFilter {
    Q_OBJECT
public:
    KeyScribeEscape() : InputEventFilter(InputFilterOrder::ScreenEdge) {
        auto bus = QDBusConnection::sessionBus();
        bus.connect(app, path, iface, "StateChanged", this, SLOT(stateChanged(bool)));
        auto watcher = new QDBusServiceWatcher(app, bus,
            QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                ++m_generation;
                m_active = false;
                if (!owner.isEmpty()) refresh();
            });
        refresh();
        input()->installInputEventFilter(this);
    }
    bool isActive() const override { return false; }
    bool keyboardKey(KeyboardKeyEvent *event) override {
        // Latch the actual scan code, because Escape can be remapped to Caps.
        // Retain repeats and release even after cancellation makes the app idle.
        if (m_consumed.contains(event->nativeScanCode)) {
            if (event->state == KeyboardKeyState::Released)
                m_consumed.remove(event->nativeScanCode);
            return true;
        }
        if (!m_active || effects->isScreenLocked() || event->key != Qt::Key_Escape ||
            event->state != KeyboardKeyState::Pressed)
            return false;
        m_consumed.insert(event->nativeScanCode);
        input()->keyboard()->addFilteredKey(event->nativeScanCode);
        m_active = false;
        ++m_generation;
        QDBusConnection::sessionBus().asyncCall(
            QDBusMessage::createMethodCall(app, path, iface, "Cancel"), 1000);
        return true;
    }
private Q_SLOTS:
    void stateChanged(bool active) {
        ++m_generation;
        m_active = active;
    }
private:
    void refresh() {
        const auto generation = ++m_generation;
        auto call = QDBusConnection::sessionBus().asyncCall(
            QDBusMessage::createMethodCall(app, path, iface, "GetState"), 1000);
        auto watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *watcher) {
                QDBusPendingReply<bool> reply = *watcher;
                if (generation == m_generation)
                    m_active = !reply.isError() && reply.value();
                watcher->deleteLater();
            });
    }
    bool m_active = false;
    unsigned m_generation = 0;
    QSet<uint32_t> m_consumed;
};
}
KWIN_EFFECT_FACTORY(KWin::KeyScribeEscape, "metadata.json")
#include "escape.moc"
