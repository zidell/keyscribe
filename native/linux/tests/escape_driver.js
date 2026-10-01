import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
const keyboard = global.stage.context.get_backend().get_default_seat().create_virtual_device(Clutter.InputDeviceType.KEYBOARD_DEVICE);
const service = Gio.DBusExportedObject.wrapJSObject(`<node><interface name="net.gitools.keyscribe.Driver"><method name="Key"><arg type="u" direction="in"/><arg type="b" direction="in"/></method><signal name="Record"><arg type="b"/></signal></interface></node>`, {
    Key(symbol, down) {
        Main.overview.hide();
        keyboard.notify_keyval(global.get_current_time() * 1000, symbol,
            down ? Clutter.KeyState.PRESSED : Clutter.KeyState.RELEASED);
    },
});
service.export(Gio.DBus.session, '/net/gitools/keyscribe/Driver');
Gio.bus_own_name_on_connection(Gio.DBus.session, 'net.gitools.keyscribe.Driver', Gio.BusNameOwnerFlags.NONE, null, null);

const action = global.display.grab_accelerator('F12', Meta.KeyBindingFlags.TRIGGER_RELEASE);
Main.wm.allowKeybinding(Meta.external_binding_name_for_action(action), Shell.ActionMode.NORMAL);
global.display.connect('accelerator-activated', (_display, id) => {
    if (id === action) service.emit_signal('Record', new GLib.Variant('(b)', [true]));
});
global.display.connect('accelerator-deactivated', (_display, id) => {
    if (id === action) service.emit_signal('Record', new GLib.Variant('(b)', [false]));
});
