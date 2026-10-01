import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import St from 'gi://St';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const APP = 'net.gitools.keyscribe';
const PATH = '/net/gitools/keyscribe/Escape';
const IFACE = 'net.gitools.keyscribe.Escape';

export default class KeyScribeEscape extends Extension {
    enable() {
        this._active = false;
        this._down = false;
        this._generation = 0;
        this._grabs = [];
        this._overrides = [];
        this._pressed = global.display.connect('accelerator-activated', (_display, action) => {
            if (!this._grabs.includes(action)) return;
            this._down = true;
            this._cancel();
        });
        this._released = global.display.connect('accelerator-deactivated', (_display, action) => {
            if (!this._grabs.includes(action)) return;
            this._down = false;
            this._syncGrabs();
        });
        this._watch = Gio.bus_watch_name(Gio.BusType.SESSION, APP,
            Gio.BusNameWatcherFlags.NONE,
            (bus, name, owner) => {
                this._owner = owner;
                this._subscription = bus.signal_subscribe(owner, IFACE,
                    'StateChanged', PATH, null, Gio.DBusSignalFlags.NONE,
                    (_bus, _sender, _path, _iface, _signal, parameters) => {
                        this._generation++;
                        [this._active] = parameters.deep_unpack();
                        this._syncGrabs();
                    });
                const generation = ++this._generation;
                bus.call(owner, PATH, IFACE, 'GetState', null, null,
                    Gio.DBusCallFlags.NONE, 1000, null, (connection, result) => {
                        try {
                            const state = connection.call_finish(result).deep_unpack()[0];
                            if (generation === this._generation) {
                                this._active = state;
                                this._syncGrabs();
                            }
                        } catch (_) {
                            // The app may exit between name appearance and this reply.
                        }
                    });
            }, bus => {
                this._generation++;
                this._active = false;
                this._syncGrabs();
                this._owner = null;
                if (this._subscription)
                    bus.signal_unsubscribe(this._subscription);
                this._subscription = 0;
            });
        this._capture = global.stage.connect('captured-event', (_stage, event) => {
            const type = event.type();
            if (type !== Clutter.EventType.KEY_PRESS && type !== Clutter.EventType.KEY_RELEASE)
                return Clutter.EVENT_PROPAGATE;
            if (event.get_key_symbol() !== Clutter.KEY_Escape || (!this._active && !this._down))
                return Clutter.EVENT_PROPAGATE;
            if (type === Clutter.EventType.KEY_PRESS && !this._down) {
                this._down = true;
                this._cancel();
            } else if (type === Clutter.EventType.KEY_RELEASE) {
                this._down = false;
                this._syncGrabs();
            }
            // Keep consuming repeats and the release after the app becomes idle.
            return Clutter.EVENT_STOP;
        });
    }

    _cancel() {
        if (!this._active || !this._owner) return;
        this._active = false;
        Gio.DBus.session.call(this._owner, PATH, IFACE, 'Cancel', null, null,
            Gio.DBusCallFlags.NONE, 1000, null, (bus, result) => {
                try { bus.call_finish(result); } catch (_) { /* App exited. */ }
            });
    }

    _syncGrabs() {
        if (this._active || this._down) {
            if (this._grabs.length) return;
            this._overrideConflicts(true);
            const modifiers = ['<Shift>', '<Control>', '<Alt>', '<Super>', '<Mod5>'];
            for (let mask = 0; mask < (1 << modifiers.length); mask++) {
                const key = modifiers.filter((_modifier, bit) => mask & (1 << bit)).join('') + 'Escape';
                const action = global.display.grab_accelerator(key,
                    Meta.KeyBindingFlags.TRIGGER_RELEASE | Meta.KeyBindingFlags.IGNORE_AUTOREPEAT);
                if (action === Meta.KeyBindingAction.NONE) continue;
                Main.wm.allowKeybinding(Meta.external_binding_name_for_action(action), Shell.ActionMode.ALL);
                this._grabs.push(action);
            }
        } else {
            if (this._modalGrab) {
                Main.popModal(this._modalGrab);
                this._modalGrab = null;
                this._modalActor.destroy();
                this._modalActor = null;
            }
            this._overrideConflicts(false);
            for (const action of this._grabs) {
                Main.wm.allowKeybinding(Meta.external_binding_name_for_action(action), Shell.ActionMode.NONE);
                global.display.ungrab_accelerator(action);
            }
            this._grabs = [];
        }
    }

    _overrideConflicts(enabled) {
        if (enabled && this._overrides.length) return;
        const candidates = {
            'org.gnome.desktop.wm.keybindings': ['cycle-windows', 'cycle-windows-backward',
                'cycle-panels', 'cycle-panels-backward'],
            'org.gnome.mutter.wayland.keybindings': ['restore-shortcuts'],
            'org.gnome.mutter.keybindings': ['cancel-input-capture'],
        };
        if (enabled) {
            for (const [schema, names] of Object.entries(candidates)) {
                const settings = new Gio.Settings({schema_id: schema});
                for (const name of names) {
                    const keys = settings.get_strv(name);
                    // Preserve reassigned non-Escape desktop actions.
                    if (keys.length && keys.every(key => key.endsWith('Escape')))
                        this._overrides.push(name);
                }
            }
        }
        for (const name of this._overrides) {
            const original = name.startsWith('cycle-windows')
                ? Main.wm._startSwitcher.bind(Main.wm) : null;
            Meta.keybindings_set_custom_handler(name, enabled ? () => {
                this._down = true;
                // Keep focus in Shell until Escape is released. NORMAL retains
                // the recording portal's press/release shortcuts during this grab.
                if (!this._modalGrab) {
                    this._modalActor = new St.Widget({reactive: true, opacity: 0});
                    Main.uiGroup.add_child(this._modalActor);
                    this._modalGrab = Main.pushModal(this._modalActor,
                        {actionMode: Shell.ActionMode.NORMAL});
                }
                this._cancel();
            } : original);
        }
        if (!enabled) this._overrides = [];
    }

    disable() {
        this._generation++;
        global.stage.disconnect(this._capture);
        global.display.disconnect(this._pressed);
        global.display.disconnect(this._released);
        this._active = this._down = false;
        this._syncGrabs();
        Gio.bus_unwatch_name(this._watch);
        if (this._subscription)
            Gio.DBus.session.signal_unsubscribe(this._subscription);
        this._subscription = 0;
        this._active = this._down = false;
    }
}
