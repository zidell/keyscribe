#include "portal.h"
#include <string.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixfdlist.h>
#include <unistd.h>
#include <stdarg.h>
#include <glib/gstdio.h>
#define DEST "org.freedesktop.portal.Desktop"
#define PATH "/org/freedesktop/portal/desktop"
#define SHORTCUTS "org.freedesktop.portal.GlobalShortcuts"
#define REMOTE "org.freedesktop.portal.RemoteDesktop"

static void trace(Portal *p, const char *format, ...) G_GNUC_PRINTF(2, 3);
static void trace(Portal *p, const char *format, ...) {
    if (!p->trace)
        return;
    va_list args;
    va_start(args, format);
    char *message = g_strdup_vprintf(format, args);
    va_end(args);
    p->trace(message);
    g_free(message);
}

// Mutter passes focused Wayland text-input keys through IBus before checking
// global shortcuts. Reserve Hangul while it is our trigger, otherwise IBus
// consumes it as a language toggle and the portal never receives the key.
static const char *ibus_schemas[] = {"org.freedesktop.ibus.engine.hangul",
                                    "org.freedesktop.ibus.general.hotkey"};
static const char *ibus_keys[] = {"switch-keys", "trigger"};
static GSettings *ibus_settings(guint index) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = source ?
        g_settings_schema_source_lookup(source, ibus_schemas[index], TRUE) : NULL;
    return schema && g_settings_schema_has_key(schema, ibus_keys[index])
               ? g_settings_new_full(schema, NULL, NULL) : NULL;
}
static char *ibus_backup_path(void) {
    return g_build_filename(g_get_user_config_dir(), "keyscribe",
                            "ibus-shortcut-backup.ini", NULL);
}
static void ibus_restore(Portal *p) {
    g_autofree char *path = ibus_backup_path();
    g_autoptr(GKeyFile) backup = g_key_file_new();
    if (!g_key_file_load_from_file(backup, path, G_KEY_FILE_NONE, NULL))
        return;
    for (guint i = 0; i < G_N_ELEMENTS(ibus_schemas); i++) {
        g_autoptr(GSettings) settings = ibus_settings(i);
        if (!settings)
            continue;
        g_autofree char *original = g_key_file_get_value(backup, ibus_schemas[i], "original", NULL);
        g_autofree char *applied = g_key_file_get_value(backup, ibus_schemas[i], "applied", NULL);
        g_autoptr(GVariant) current = g_settings_get_value(settings, ibus_keys[i]);
        g_autoptr(GVariant) old = original ?
            g_variant_parse(g_variant_get_type(current), original, NULL, NULL, NULL) : NULL;
        g_autoptr(GVariant) reserved = applied ?
            g_variant_parse(g_variant_get_type(current), applied, NULL, NULL, NULL) : NULL;
        // Do not overwrite settings the user changed while KeyScribe was running.
        if (old && reserved && g_variant_equal(current, reserved)) {
            g_settings_set_value(settings, ibus_keys[i], old);
            trace(p, "ibus restored schema=%s key=%s", ibus_schemas[i], ibus_keys[i]);
        }
    }
    g_settings_sync();
    g_unlink(path);
}
static void ibus_reserve(Portal *p) {
    // Only touch desktop preferences on the real GNOME portal, never a fixture
    // bus or another desktop's shortcut implementation.
    g_autoptr(GVariant) owner = g_dbus_connection_call_sync(
        p->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "NameHasOwner", g_variant_new("(s)", "org.freedesktop.impl.portal.desktop.gnome"),
        G_VARIANT_TYPE("(b)"), 0, 3000, NULL, NULL);
    gboolean gnome = FALSE;
    if (owner)
        g_variant_get(owner, "(b)", &gnome);
    if (!gnome)
        return;
    p->ibus_managed = TRUE;
    ibus_restore(p);
    if (g_strcmp0(p->preferred_trigger, "Hangul"))
        return;
    g_autoptr(GKeyFile) backup = g_key_file_new();
    GSettings *settings[2] = {NULL, NULL};
    GVariant *replacements[2] = {NULL, NULL};
    gboolean changed = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(ibus_schemas); i++) {
        settings[i] = ibus_settings(i);
        if (!settings[i])
            continue;
        g_autoptr(GVariant) current = g_settings_get_value(settings[i], ibus_keys[i]);
        g_auto(GStrv) keys = i == 0 ? g_strsplit(g_variant_get_string(current, NULL), ",", -1)
                                   : g_variant_dup_strv(current, NULL);
        GPtrArray *kept = g_ptr_array_new();
        gboolean removed = FALSE;
        for (guint j = 0; keys[j]; j++) {
            if (g_str_equal(g_strstrip(keys[j]), "Hangul"))
                removed = TRUE;
            else
                g_ptr_array_add(kept, keys[j]);
        }
        g_ptr_array_add(kept, NULL);
        if (removed) {
            if (i == 0) {
                g_autofree char *joined = g_strjoinv(",", (char **)kept->pdata);
                replacements[i] = g_variant_ref_sink(g_variant_new_string(joined));
            } else {
                replacements[i] = g_variant_ref_sink(
                    g_variant_new_strv((const char *const *)kept->pdata, -1));
            }
            g_autofree char *old = g_variant_print(current, TRUE);
            g_autofree char *replacement = g_variant_print(replacements[i], TRUE);
            g_key_file_set_value(backup, ibus_schemas[i], "original", old);
            g_key_file_set_value(backup, ibus_schemas[i], "applied", replacement);
            changed = TRUE;
        }
        g_ptr_array_unref(kept);
    }
    g_autofree char *path = ibus_backup_path();
    g_autofree char *directory = g_path_get_dirname(path);
    g_mkdir_with_parents(directory, 0700);
    // Save recovery data before changing IBus; the next launch can recover
    // after a crash as well as after a normal exit.
    if (changed && g_key_file_save_to_file(backup, path, NULL)) {
        g_chmod(path, 0600);
        for (guint i = 0; i < G_N_ELEMENTS(ibus_schemas); i++)
            if (replacements[i]) {
                g_settings_set_value(settings[i], ibus_keys[i], replacements[i]);
                trace(p, "ibus reserved Hangul schema=%s key=%s", ibus_schemas[i], ibus_keys[i]);
            }
        g_settings_sync();
    }
    for (guint i = 0; i < G_N_ELEMENTS(ibus_schemas); i++) {
        g_clear_object(&settings[i]);
        g_clear_pointer(&replacements[i], g_variant_unref);
    }
}

typedef struct {
    Portal *p;
    PortalResult callback;
    void *user;
    char *path;
    guint subscription;
    guint references;
    gboolean done;
} Request;
static GVariant *empty(void) {
    return g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
}
static void request_free(Request *r) {
    if (--r->references)
        return;
    if (r->subscription)
        g_dbus_connection_signal_unsubscribe(r->p->bus, r->subscription);
    g_free(r->path);
    g_free(r);
}
static void response(GDBusConnection *bus, const char *sender, const char *path, const char *iface,
                     const char *signal, GVariant *params, void *user) {
    (void)bus;
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;
    Request *r = user;
    r->done = TRUE;
    guint code;
    GVariant *values;
    g_variant_get(params, "(u@a{sv})", &code, &values);
    g_autoptr(GError) error = NULL;
    if (code)
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                    "시스템 권한 요청이 취소되었거나 거부되었습니다");
    r->callback(values, error, r->user);
    g_variant_unref(values);
    g_dbus_connection_signal_unsubscribe(r->p->bus, r->subscription);
    r->subscription = 0;
    request_free(r);
}
static void call_done(GObject *obj, GAsyncResult *result, void *user) {
    Request *r = user;
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) reply =
        g_dbus_connection_call_finish(G_DBUS_CONNECTION(obj), result, &error);
    if (r->done) {
        request_free(r);
        return;
    }
    if (!reply) {
        r->done = TRUE;
        r->callback(NULL, error, r->user);
        request_free(r);
        request_free(r);
        return;
    }
    // handle_token fixes the path, so subscribing before the method avoids a Response race.
    const char *path;
    g_variant_get(reply, "(&o)", &path);
    if (g_strcmp0(path, r->path)) {
        g_dbus_connection_signal_unsubscribe(r->p->bus, r->subscription);
        r->subscription =
            g_dbus_connection_signal_subscribe(r->p->bus, DEST, "org.freedesktop.portal.Request",
                                               "Response", path, NULL, 0, response, r, NULL);
        g_free(r->path);
        r->path = g_strdup(path);
    }
    request_free(r);
}
static char *token(void) {
    static guint n;
    return g_strdup_printf("keyscribe_%u_%u", g_random_int(), ++n);
}
static GVariant *options(const char *t, const char *session) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "handle_token", g_variant_new_string(t));
    if (session)
        g_variant_builder_add(&b, "{sv}", "session_handle_token", g_variant_new_string(session));
    return g_variant_builder_end(&b);
}
static void request(Portal *p, const char *iface, const char *method, const char *t, GVariant *args,
                    PortalResult cb, void *user) {
    if (!p->bus) {
        g_autoptr(GError) error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                                                      "데스크톱 포털에 연결되지 않았습니다");
        g_variant_ref_sink(args);
        g_variant_unref(args);
        cb(NULL, error, user);
        return;
    }
    g_autofree char *sender = g_strdup(g_dbus_connection_get_unique_name(p->bus) + 1);
    for (char *c = sender; *c; c++)
        if (*c == '.')
            *c = '_';
    Request *r = g_new0(Request, 1);
    r->references = 2;
    r->p = p;
    r->callback = cb;
    r->user = user;
    r->path = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", sender, t);
    r->subscription =
        g_dbus_connection_signal_subscribe(p->bus, DEST, "org.freedesktop.portal.Request",
                                           "Response", r->path, NULL, 0, response, r, NULL);
    g_dbus_connection_call(p->bus, DEST, PATH, iface, method, args, G_VARIANT_TYPE("(o)"), 0, 30000,
                           NULL, call_done, r);
}
static void clipboard_transfer(GDBusConnection *, const char *, const char *, const char *,
                               const char *, GVariant *, void *);
static void shortcut_signal(GDBusConnection *bus, const char *sender, const char *path,
                            const char *iface, const char *signal, GVariant *args, void *user) {
    (void)bus;
    (void)sender;
    (void)path;
    (void)iface;
    Portal *p = user;
    if (!g_str_equal(signal, "Activated") && !g_str_equal(signal, "Deactivated"))
        return;
    const char *session, *id;
    guint64 stamp;
    GVariant *opts;
    g_variant_get(args, "(&o&st@a{sv})", &session, &id, &stamp, &opts);
    trace(p, "portal %s id=%s session=%s current=%s matched=%d", signal, id,
          session, p->shortcuts ? p->shortcuts : "none",
          g_strcmp0(session, p->shortcuts) == 0);
    if (g_strcmp0(session, p->shortcuts) == 0)
        p->event(id, g_str_equal(signal, "Activated"), p->user);
    g_variant_unref(opts);
}
static void session_closed(GDBusConnection *bus, const char *sender, const char *path,
                           const char *iface, const char *signal, GVariant *args, void *user) {
    (void)bus;
    (void)sender;
    (void)iface;
    (void)signal;
    (void)args;
    Portal *p = user;
    trace(p, "portal session closed path=%s", path);
    if (!g_strcmp0(path, p->keyboard)) {
        g_clear_pointer(&p->keyboard, g_free);
        p->clipboard_enabled = FALSE;
    }
    if (!g_strcmp0(path, p->shortcuts))
        g_clear_pointer(&p->shortcuts, g_free);
}
gboolean portal_init(Portal *p, ShortcutEvent event, void *user, GError **error) {
    *p = (Portal){.event = event, .user = user};
    p->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error);
    if (!p->bus)
        return FALSE;
    // New portals require an application identity for unsandboxed native applications.
    g_autoptr(GVariant) registered = g_dbus_connection_call_sync(
        p->bus, DEST, PATH, "org.freedesktop.host.portal.Registry", "Register",
        g_variant_new("(s@a{sv})", "net.gitools.keyscribe", empty()), NULL, 0, 5000, NULL, NULL);
    p->shortcut_subscription = g_dbus_connection_signal_subscribe(
        p->bus, DEST, SHORTCUTS, NULL, PATH, NULL, 0, shortcut_signal, p, NULL);
    p->clipboard_subscription = g_dbus_connection_signal_subscribe(
        p->bus, DEST, "org.freedesktop.portal.Clipboard", "SelectionTransfer", PATH, NULL, 0,
        clipboard_transfer, p, NULL);
    p->closed_subscription =
        g_dbus_connection_signal_subscribe(p->bus, DEST, "org.freedesktop.portal.Session", "Closed",
                                           NULL, NULL, 0, session_closed, p, NULL);
    return TRUE;
}
static void close_session(Portal *p, char **session) {
    if (*session) {
        trace(p, "portal closing session=%s", *session);
        g_dbus_connection_call(p->bus, DEST, *session, "org.freedesktop.portal.Session", "Close",
                               NULL, NULL, 0, 5000, NULL, NULL, NULL);
        g_clear_pointer(session, g_free);
    }
}
void portal_clear(Portal *p) {
    if (!p->bus)
        return;
    if (p->ibus_managed)
        ibus_restore(p);
    close_session(p, &p->shortcuts);
    close_session(p, &p->keyboard);
    g_dbus_connection_signal_unsubscribe(p->bus, p->shortcut_subscription);
    g_dbus_connection_signal_unsubscribe(p->bus, p->closed_subscription);
    g_dbus_connection_signal_unsubscribe(p->bus, p->clipboard_subscription);
    g_clear_pointer(&p->clipboard_text, g_free);
    g_clear_pointer(&p->preferred_trigger, g_free);
    g_clear_pointer(&p->keyboard_token_path, g_free);
    g_clear_pointer(&p->keyboard_restore_token, g_free);
    g_clear_object(&p->bus);
}
typedef struct {
    Portal *p;
    PortalResult cb;
    void *user;
    gboolean keyboard;
} Setup;
static void finish(GVariant *values, const GError *error, void *user) {
    Setup *s = user;
    g_autoptr(GError) local = NULL;
    trace(s->p, "portal setup response kind=%s session=%s error=%s",
          s->keyboard ? "keyboard" : "shortcuts",
          s->keyboard ? (s->p->keyboard ? s->p->keyboard : "none")
                      : (s->p->shortcuts ? s->p->shortcuts : "none"),
          error ? error->message : "none");
    if (!error && s->keyboard) {
        g_variant_lookup(values, "clipboard_enabled", "b", &s->p->clipboard_enabled);
        guint devices = 0;
        g_variant_lookup(values, "devices", "u", &devices);
        if (!(devices & 1))
            local = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                        "키보드 입력 권한이 허용되지 않았습니다");
        const char *token = NULL;
        if (!local && g_variant_lookup(values, "restore_token", "&s", &token)) {
            g_free(s->p->keyboard_restore_token);
            s->p->keyboard_restore_token = g_strdup(token);
            if (s->p->keyboard_token_path) {
                g_autoptr(GError) save_error = NULL;
                if (g_file_set_contents_full(s->p->keyboard_token_path, token, -1,
                        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
                        0600, &save_error))
                    trace(s->p, "keyboard permission restore token saved");
                else
                    trace(s->p, "keyboard permission persistence failed: %s", save_error->message);
            }
        }
    }
    if (!error && !s->keyboard) {
        g_autoptr(GVariant) shortcuts =
            g_variant_lookup_value(values, "shortcuts", G_VARIANT_TYPE("a(sa{sv})"));
        if (!shortcuts || !g_variant_n_children(shortcuts))
            local = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                        "등록된 전역 단축키가 없습니다");
    }
    if (error || local) {
        close_session(s->p, s->keyboard ? &s->p->keyboard : &s->p->shortcuts);
        if (s->keyboard) {
            g_clear_pointer(&s->p->keyboard_restore_token, g_free);
            if (s->p->keyboard_token_path)
                g_unlink(s->p->keyboard_token_path);
        } else if (s->p->ibus_managed) {
            ibus_restore(s->p);
        }
    }
    if (s->keyboard)
        s->p->keyboard_pending = FALSE;
    else
        s->p->shortcuts_pending = FALSE;
    s->cb(values, error ? error : local, s->user);
    g_free(s);
}
static void selected(GVariant *values, const GError *error, void *user) {
    Setup *s = user;
    if (error) {
        finish(values, error, s);
        return;
    }
    g_autoptr(GVariant) clipboard = g_dbus_connection_call_sync(
        s->p->bus, DEST, PATH, "org.freedesktop.portal.Clipboard", "RequestClipboard",
        g_variant_new("(o@a{sv})", s->p->keyboard, empty()), NULL, 0, 5000, NULL, NULL);
    g_autofree char *t = token();
    request(s->p, REMOTE, "Start", t,
            g_variant_new("(os@a{sv})", s->p->keyboard, "", options(t, NULL)), finish, s);
}
static void created(GVariant *values, const GError *error, void *user) {
    Setup *s = user;
    if (error) {
        finish(values, error, s);
        return;
    }
    const char *session = NULL;
    g_variant_lookup(values, "session_handle", "&s", &session);
    if (!session || !g_variant_is_object_path(session)) {
        g_autoptr(GError) e =
            g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "포털 세션 응답 오류");
        finish(values, e, s);
        return;
    }
    g_autofree char *t = token();
    if (s->keyboard) {
        s->p->keyboard = g_strdup(session);
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&b, "{sv}", "handle_token", g_variant_new_string(t));
        g_variant_builder_add(&b, "{sv}", "types", g_variant_new_uint32(1));
        g_variant_builder_add(&b, "{sv}", "persist_mode", g_variant_new_uint32(2));
        if (s->p->keyboard_restore_token && *s->p->keyboard_restore_token)
            g_variant_builder_add(&b, "{sv}", "restore_token",
                                  g_variant_new_string(s->p->keyboard_restore_token));
        request(s->p, REMOTE, "SelectDevices", t,
                g_variant_new("(o@a{sv})", session, g_variant_builder_end(&b)), selected, s);
    } else {
        s->p->shortcuts = g_strdup(session);
        GVariantBuilder list;
        g_variant_builder_init(&list, G_VARIANT_TYPE("a(sa{sv})"));
        const char *ids[] = {"record"}, *descs[] = {"KeyScribe 녹음"},
                   *triggers[] = {s->p->preferred_trigger ? s->p->preferred_trigger
                                                          : "CTRL+ALT+space"};
        for (guint i = 0; i < G_N_ELEMENTS(ids); i++) {
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
            g_variant_builder_add(&b, "{sv}", "description", g_variant_new_string(descs[i]));
            g_variant_builder_add(&b, "{sv}", "preferred_trigger",
                                  g_variant_new_string(triggers[i]));
            g_variant_builder_add(&list, "(s@a{sv})", ids[i], g_variant_builder_end(&b));
        }
        request(s->p, SHORTCUTS, "BindShortcuts", t,
                g_variant_new("(o@a(sa{sv})s@a{sv})", session, g_variant_builder_end(&list), "",
                              options(t, NULL)),
                finish, s);
    }
}
static void setup(Portal *p, gboolean keyboard, PortalResult cb, void *user) {
    if (keyboard ? p->keyboard_pending : p->shortcuts_pending)
        return;
    if (keyboard)
        p->keyboard_pending = TRUE;
    else
        p->shortcuts_pending = TRUE;
    close_session(p, keyboard ? &p->keyboard : &p->shortcuts);
    Setup *s = g_new0(Setup, 1);
    s->p = p;
    s->cb = cb;
    s->user = user;
    s->keyboard = keyboard;
    g_autofree char *t = token();
    g_autofree char *session = token();
    request(p, keyboard ? REMOTE : SHORTCUTS, "CreateSession", t,
            g_variant_new("(@a{sv})", options(t, session)), created, s);
}
void portal_bind(Portal *p, PortalResult cb, void *user) {
    trace(p, "portal bind requested trigger=%s pending=%d",
          p->preferred_trigger ? p->preferred_trigger : "CTRL+ALT+space",
          p->shortcuts_pending);
    ibus_reserve(p);
    // GNOME reuses previously saved bindings and ignores preferred_trigger for
    // existing IDs. Apply the user's selection to this app's existing binding,
    // then let the portal create the authorized live session as usual.
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = source ? g_settings_schema_source_lookup(
        source, "org.gnome.settings-daemon.global-shortcuts.application", TRUE) : NULL;
    if (schema) {
        g_autoptr(GSettings) settings = g_settings_new_full(
            schema, NULL, "/org/gnome/settings-daemon/global-shortcuts/net.gitools.keyscribe/");
        g_autoptr(GVariant) saved = g_settings_get_value(settings, "shortcuts");
        GVariantBuilder kept;
        g_variant_builder_init(&kept, G_VARIANT_TYPE("a(sa{sv})"));
        gboolean changed = FALSE;
        for (gsize i = 0; i < g_variant_n_children(saved); i++) {
            g_autoptr(GVariant) item = g_variant_get_child_value(saved, i);
            g_autoptr(GVariant) id = g_variant_get_child_value(item, 0);
            const char *name = g_variant_get_string(id, NULL);
            if (g_str_equal(name, "cancel")) {
                changed = TRUE;
            } else if (g_str_equal(name, "record") && p->preferred_trigger) {
                g_auto(GStrv) parts = g_strsplit(p->preferred_trigger, "+", -1);
                GString *accel = g_string_new(NULL);
                guint count = g_strv_length(parts);
                for (guint j = 0; j + 1 < count; j++) {
                    const char *mod = g_str_equal(parts[j], "CTRL") ? "<Control>"
                                      : g_str_equal(parts[j], "ALT") ? "<Alt>"
                                      : g_str_equal(parts[j], "SHIFT") ? "<Shift>"
                                      : g_str_equal(parts[j], "LOGO") ||
                                                g_str_equal(parts[j], "SUPER") ? "<Super>"
                                      : g_str_equal(parts[j], "NUM") ? "<Mod2>" : "";
                    g_string_append(accel, mod);
                }
                if (count)
                    g_string_append(accel, parts[count - 1]);
                g_autoptr(GVariant) properties = g_variant_get_child_value(item, 1);
                GVariantDict dict;
                g_variant_dict_init(&dict, properties);
                const char *bindings[] = {accel->str, NULL};
                g_variant_dict_insert_value(&dict, "shortcuts", g_variant_new_strv(bindings, 1));
                g_variant_builder_add(&kept, "(s@a{sv})", "record", g_variant_dict_end(&dict));
                g_string_free(accel, TRUE);
                changed = TRUE;
            } else {
                g_variant_builder_add_value(&kept, item);
            }
        }
        if (changed)
            g_settings_set_value(settings, "shortcuts", g_variant_builder_end(&kept));
        else
            g_variant_builder_clear(&kept);
    }
    setup(p, FALSE, cb, user);
}
void portal_enable_keyboard(Portal *p, PortalResult cb, void *user) {
    setup(p, TRUE, cb, user);
}
void portal_pause_shortcuts(Portal *p) {
    close_session(p, &p->shortcuts);
}
gboolean portal_key(Portal *p, int keysym, gboolean pressed, GError **error) {
    if (!p->bus || !p->keyboard) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "자동 붙여넣기 권한을 먼저 허용하세요");
        return FALSE;
    }
    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        p->bus, DEST, PATH, REMOTE, "NotifyKeyboardKeysym",
        g_variant_new("(o@a{sv}iu)", p->keyboard, empty(), keysym, pressed ? 1 : 0), NULL, 0, 3000,
        NULL, error);
    return reply != NULL;
}

typedef struct {
    GDBusConnection *bus;
    char *session, *text;
    guint serial;
    GOutputStream *stream;
} ClipboardWrite;
static void clipboard_written(GObject *obj, GAsyncResult *result, void *user) {
    ClipboardWrite *w = user;
    g_autoptr(GError) error = NULL;
    gsize written = 0;
    gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(obj), result, &written, &error);
    g_output_stream_close(w->stream, NULL, NULL);
    g_dbus_connection_call(w->bus, DEST, PATH, "org.freedesktop.portal.Clipboard",
                           "SelectionWriteDone", g_variant_new("(oub)", w->session, w->serial, ok),
                           NULL, 0, 5000, NULL, NULL, NULL);
    g_object_unref(w->stream);
    g_object_unref(w->bus);
    g_free(w->session);
    g_free(w->text);
    g_free(w);
}
static void clipboard_transfer(GDBusConnection *bus, const char *sender, const char *path,
                               const char *iface, const char *signal, GVariant *params,
                               void *user) {
    (void)sender;
    (void)path;
    (void)iface;
    (void)signal;
    Portal *p = user;
    const char *session, *mime;
    guint serial;
    g_variant_get(params, "(&o&su)", &session, &mime, &serial);
    (void)mime;
    if (g_strcmp0(session, p->keyboard) || !p->clipboard_text)
        return;
    g_autoptr(GUnixFDList) fds = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) reply = g_dbus_connection_call_with_unix_fd_list_sync(
        bus, DEST, PATH, "org.freedesktop.portal.Clipboard", "SelectionWrite",
        g_variant_new("(ou)", session, serial), G_VARIANT_TYPE("(h)"), 0, 5000, NULL, &fds, NULL,
        &error);
    if (!reply)
        return;
    gint handle;
    g_variant_get(reply, "(h)", &handle);
    int fd = g_unix_fd_list_get(fds, handle, &error);
    if (fd < 0)
        return;
    ClipboardWrite *w = g_new0(ClipboardWrite, 1);
    w->bus = g_object_ref(bus);
    w->session = g_strdup(session);
    w->text = g_strdup(p->clipboard_text);
    w->serial = serial;
    w->stream = g_unix_output_stream_new(fd, TRUE);
    g_output_stream_write_all_async(w->stream, w->text, strlen(w->text), G_PRIORITY_DEFAULT, NULL,
                                    clipboard_written, w);
}
gboolean portal_set_text(Portal *p, const char *text, GError **error) {
    // Older portals can still use GtkClipboard with an authorized keyboard session.
    if (!p->clipboard_enabled)
        return TRUE;
    g_free(p->clipboard_text);
    p->clipboard_text = g_strdup(text);
    const char *types[] = {"text/plain;charset=utf-8", "text/plain", NULL};
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "mime_types", g_variant_new_strv(types, -1));
    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        p->bus, DEST, PATH, "org.freedesktop.portal.Clipboard", "SetSelection",
        g_variant_new("(o@a{sv})", p->keyboard, g_variant_builder_end(&b)), NULL, 0, 5000, NULL,
        error);
    return reply != NULL;
}
