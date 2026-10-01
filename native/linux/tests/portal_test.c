#include "portal.h"
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <string.h>

#define DEST "org.freedesktop.portal.Desktop"
#define PATH "/org/freedesktop/portal/desktop"
#define SHORT "org.freedesktop.portal.GlobalShortcuts"
#define REMOTE "org.freedesktop.portal.RemoteDesktop"
#define CLIP "org.freedesktop.portal.Clipboard"
static const char xml[] =
    "<node>"
    "<interface name='org.freedesktop.host.portal.Registry'><method name='Register'><arg type='s' "
    "direction='in'/><arg type='a{sv}' direction='in'/></method></interface>"
    "<interface name='" SHORT
    "'><method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' "
    "direction='out'/></method><method name='BindShortcuts'><arg type='o' direction='in'/><arg "
    "type='a(sa{sv})' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' "
    "direction='in'/><arg type='o' direction='out'/></method></interface>"
    "<interface name='" REMOTE
    "'><method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' "
    "direction='out'/></method><method name='SelectDevices'><arg type='o' direction='in'/><arg "
    "type='a{sv}' direction='in'/><arg type='o' direction='out'/></method><method "
    "name='Start'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' "
    "direction='in'/><arg type='o' direction='out'/></method><method "
    "name='NotifyKeyboardKeysym'><arg type='o' direction='in'/><arg type='a{sv}' "
    "direction='in'/><arg type='i' direction='in'/><arg type='u' "
    "direction='in'/></method></interface>"
    "<interface name='" CLIP
    "'><method name='RequestClipboard'><arg type='o' direction='in'/><arg type='a{sv}' "
    "direction='in'/></method><method name='SetSelection'><arg type='o' direction='in'/><arg "
    "type='a{sv}' direction='in'/></method><method name='SelectionWrite'><arg type='o' "
    "direction='in'/><arg type='u' direction='in'/><arg type='h' direction='out'/></method><method "
    "name='SelectionWriteDone'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg "
    "type='b' direction='in'/></method></interface>"
    "<interface name='org.freedesktop.portal.Session'><method name='Close'/></interface>"
    "</node>";
static GMutex mutex;
static GCond cond;
static gboolean ready;
static GMainLoop *service_loop;
static gint key_events, clipboard_done, deny;
static int read_fd = -1;
static char *keyboard_session, *shortcut_session;
static void method(GDBusConnection *bus, const char *sender, const char *path, const char *iface,
                   const char *name, GVariant *args, GDBusMethodInvocation *call, void *user) {
    (void)path;
    (void)user;
    if (g_str_equal(name, "Register") || g_str_equal(name, "Close") ||
        g_str_equal(name, "RequestClipboard")) {
        g_dbus_method_invocation_return_value(call, NULL);
        return;
    }
    if (g_str_equal(name, "NotifyKeyboardKeysym")) {
        g_atomic_int_inc(&key_events);
        g_dbus_method_invocation_return_value(call, NULL);
        return;
    }
    if (g_str_equal(name, "SetSelection")) {
        const char *session;
        GVariant *opts;
        g_variant_get(args, "(&o@a{sv})", &session, &opts);
        g_autoptr(GVariant) types =
            g_variant_lookup_value(opts, "mime_types", G_VARIANT_TYPE_STRING_ARRAY);
        g_assert_nonnull(types);
        g_assert_cmpuint(g_variant_n_children(types), ==, 2);
        g_variant_unref(opts);
        g_dbus_method_invocation_return_value(call, NULL);
        g_dbus_connection_emit_signal(
            bus, sender, PATH, CLIP, "SelectionTransfer",
            g_variant_new("(osu)", session, "text/plain;charset=utf-8", 1), NULL);
        return;
    }
    if (g_str_equal(name, "SelectionWrite")) {
        int pipe_fd[2];
        g_assert_cmpint(pipe(pipe_fd), ==, 0);
        read_fd = pipe_fd[0];
        g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
        int handle = g_unix_fd_list_append(fds, pipe_fd[1], NULL);
        close(pipe_fd[1]);
        g_dbus_method_invocation_return_value_with_unix_fd_list(call, g_variant_new("(h)", handle),
                                                                fds);
        return;
    }
    if (g_str_equal(name, "SelectionWriteDone")) {
        const char *session;
        guint serial;
        gboolean success;
        g_variant_get(args, "(&oub)", &session, &serial, &success);
        g_assert_true(success);
        char buffer[100] = {0};
        ssize_t n = read(read_fd, buffer, sizeof(buffer) - 1);
        g_assert_cmpint(n, >, 0);
        g_assert_cmpstr(buffer, ==, "한글 클립보드");
        close(read_fd);
        read_fd = -1;
        g_atomic_int_set(&clipboard_done, 1);
        g_dbus_method_invocation_return_value(call, NULL);
        g_dbus_connection_emit_signal(bus, sender, session, "org.freedesktop.portal.Session",
                                      "Closed", g_variant_new("(a{sv})", NULL), NULL);
        return;
    }
    g_autoptr(GVariant) opts = g_variant_get_child_value(args, g_variant_n_children(args) - 1);
    const char *token;
    g_assert_true(g_variant_lookup(opts, "handle_token", "&s", &token));
    g_autofree char *peer = g_strdup(sender + 1);
    for (char *c = peer; *c; c++)
        if (*c == '.')
            *c = '_';
    g_autofree char *request = g_strdup_printf(PATH "/request/%s/%s", peer, token);
    GVariantBuilder result;
    g_variant_builder_init(&result, G_VARIANT_TYPE_VARDICT);
    guint code = 0;
    if (g_str_equal(name, "CreateSession")) {
        const char *session_token;
        g_assert_true(g_variant_lookup(opts, "session_handle_token", "&s", &session_token));
        char **stored = g_str_equal(iface, REMOTE) ? &keyboard_session : &shortcut_session;
        g_free(*stored);
        *stored = g_strdup_printf(PATH "/session/%s/%s", peer, session_token);
        GDBusInterfaceVTable vtable = {.method_call = method};
        g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, NULL);
        g_dbus_connection_register_object(bus, *stored, info->interfaces[4], &vtable, NULL, NULL,
                                          NULL);
        g_variant_builder_add(&result, "{sv}", "session_handle", g_variant_new_string(*stored));
    } else if (g_str_equal(name, "BindShortcuts")) {
        g_autoptr(GVariant) requested = g_variant_get_child_value(args, 1);
        g_assert_cmpuint(g_variant_n_children(requested), ==, 1);
        g_autoptr(GVariant) first = g_variant_get_child_value(requested, 0);
        g_autoptr(GVariant) id = g_variant_get_child_value(first, 0);
        g_assert_cmpstr(g_variant_get_string(id, NULL), ==, "record");
        GVariantBuilder shortcuts;
        g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
        GVariantBuilder fields;
        g_variant_builder_init(&fields, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&fields, "{sv}", "trigger_description",
                              g_variant_new_string("Ctrl+Alt+Space"));
        g_variant_builder_add(&shortcuts, "(s@a{sv})", "record", g_variant_builder_end(&fields));
        g_variant_builder_add(&result, "{sv}", "shortcuts", g_variant_builder_end(&shortcuts));
        g_dbus_connection_emit_signal(
            bus, sender, PATH, SHORT, "ShortcutsChanged",
            g_variant_new("(o@a(sa{sv}))", shortcut_session,
                          g_variant_new_array(G_VARIANT_TYPE("(sa{sv})"), NULL, 0)),
            NULL);
    } else if (g_str_equal(name, "SelectDevices")) {
        guint types;
        g_assert_true(g_variant_lookup(opts, "types", "u", &types));
        g_assert_cmpuint(types, ==, 1);
        guint persist_mode;
        g_assert_true(g_variant_lookup(opts, "persist_mode", "u", &persist_mode));
        g_assert_cmpuint(persist_mode, ==, 2);
        const char *restored = NULL;
        if (g_variant_lookup(opts, "restore_token", "&s", &restored))
            g_assert_cmpstr(restored, ==, "fixture-keyboard-token");
    } else if (g_str_equal(name, "Start")) {
        code = g_atomic_int_get(&deny) ? 1 : 0;
        g_variant_builder_add(&result, "{sv}", "devices", g_variant_new_uint32(1));
        g_variant_builder_add(&result, "{sv}", "clipboard_enabled", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&result, "{sv}", "restore_token",
                              g_variant_new_string("fixture-keyboard-token"));
    }
    // Deliberately emit Response BEFORE the method reply to exercise lifetime/race handling.
    g_dbus_connection_emit_signal(
        bus, sender, request, "org.freedesktop.portal.Request", "Response",
        g_variant_new("(u@a{sv})", code, g_variant_builder_end(&result)), NULL);
    g_dbus_method_invocation_return_value(call, g_variant_new("(o)", request));
}
static gpointer service(gpointer address) {
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    GDBusConnection *bus =
        g_dbus_connection_new_for_address_sync(address,
                                               G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                   G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                               NULL, NULL, NULL);
    g_assert_nonnull(bus);
    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", DEST, 0), NULL, 0, 5000, NULL, NULL);
    g_assert_nonnull(reply);
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, NULL);
    GDBusInterfaceVTable vtable = {.method_call = method};
    for (int i = 0; i < 4; i++)
        g_assert_cmpuint(g_dbus_connection_register_object(bus, PATH, info->interfaces[i], &vtable,
                                                           NULL, NULL, NULL),
                         >, 0);
    service_loop = g_main_loop_new(context, FALSE);
    g_mutex_lock(&mutex);
    ready = TRUE;
    g_cond_signal(&cond);
    g_mutex_unlock(&mutex);
    g_main_loop_run(service_loop);
    g_main_loop_unref(service_loop);
    g_object_unref(bus);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return NULL;
}
static gboolean callback_done;
static gboolean expected_denial;
static void complete(GVariant *values, const GError *error, void *user) {
    (void)values;
    (void)user;
    if (expected_denial)
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    else
        g_assert_null(error);
    callback_done = TRUE;
}
static void event(const char *id, gboolean pressed, void *user) {
    (void)id;
    (void)pressed;
    (void)user;
}
static void wait_for(gboolean *done) {
    gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (!*done && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    }
    g_assert_true(*done);
}
int main(int argc, char **argv) {
    g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
    g_autofree char *config = g_dir_make_tmp("keyscribe-portal-XXXXXX", NULL);
    g_assert_nonnull(config);
    g_setenv("XDG_CONFIG_HOME", config, TRUE);
    g_test_init(&argc, &argv, NULL);
    GTestDBus *dbus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(dbus);
    GThread *thread =
        g_thread_new("fake-portal", service, (gpointer)g_test_dbus_get_bus_address(dbus));
    g_mutex_lock(&mutex);
    while (!ready)
        g_cond_wait(&cond, &mutex);
    g_mutex_unlock(&mutex);
    Portal p;
    g_autoptr(GError) error = NULL;
    g_assert_true(portal_init(&p, event, NULL, &error));
    g_assert_no_error(error);
    p.keyboard_token_path = g_build_filename(config, "keyboard-token", NULL);
    p.preferred_trigger = g_strdup("Hangul");
    GSettingsSchemaSource *schemas = g_settings_schema_source_get_default();
    g_autoptr(GSettingsSchema) schema = schemas ? g_settings_schema_source_lookup(
        schemas, "org.gnome.settings-daemon.global-shortcuts.application", TRUE) : NULL;
    g_autoptr(GSettings) saved_settings = NULL;
    if (schema) {
        saved_settings = g_settings_new_full(
            schema, NULL, "/org/gnome/settings-daemon/global-shortcuts/net.gitools.keyscribe/");
        g_autoptr(GVariant) old = g_variant_parse(
            G_VARIANT_TYPE("a(sa{sv})"),
            "[('record', {'description': <'Record'>}), "
            "('cancel', {'shortcuts': <['<Control><Alt>Escape']>})]", NULL, NULL, NULL);
        g_settings_set_value(saved_settings, "shortcuts", old);
    }
    portal_bind(&p, complete, NULL);
    wait_for(&callback_done);
    if (saved_settings) {
        g_autoptr(GVariant) saved = g_settings_get_value(saved_settings, "shortcuts");
        g_assert_cmpuint(g_variant_n_children(saved), ==, 1);
        g_autoptr(GVariant) record = g_variant_get_child_value(saved, 0);
        g_autoptr(GVariant) properties = g_variant_get_child_value(record, 1);
        g_auto(GStrv) bindings = NULL;
        g_assert_true(g_variant_lookup(properties, "shortcuts", "^as", &bindings));
        g_assert_cmpstr(bindings[0], ==, "Hangul");
        g_assert_null(bindings[1]);
    }
    g_assert_nonnull(p.shortcuts);
    g_assert_false(p.shortcuts_pending);
    portal_pause_shortcuts(&p);
    g_assert_null(p.shortcuts);
    callback_done = FALSE;
    portal_bind(&p, complete, NULL);
    wait_for(&callback_done);
    g_assert_nonnull(p.shortcuts);
    callback_done = FALSE;
    portal_enable_keyboard(&p, complete, NULL);
    wait_for(&callback_done);
    g_assert_nonnull(p.keyboard);
    g_assert_true(p.clipboard_enabled);
    g_assert_cmpstr(p.keyboard_restore_token, ==, "fixture-keyboard-token");
    g_autofree char *saved_token = NULL;
    g_assert_true(g_file_get_contents(p.keyboard_token_path, &saved_token, NULL, NULL));
    g_assert_cmpstr(saved_token, ==, "fixture-keyboard-token");
    GStatBuf metadata;
    g_assert_cmpint(g_stat(p.keyboard_token_path, &metadata), ==, 0);
    g_assert_cmpuint(metadata.st_mode & 0777, ==, 0600);
    g_assert_true(portal_key(&p, 0xffe3, TRUE, &error));
    g_assert_true(portal_key(&p, 0x76, TRUE, &error));
    g_assert_true(portal_key(&p, 0x76, FALSE, &error));
    g_assert_true(portal_key(&p, 0xffe3, FALSE, &error));
    g_assert_cmpint(g_atomic_int_get(&key_events), ==, 4);
    g_assert_true(portal_set_text(&p, "한글 클립보드", &error));
    g_assert_no_error(error);
    gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while ((!g_atomic_int_get(&clipboard_done) || p.keyboard) &&
           g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    }
    g_assert_cmpint(g_atomic_int_get(&clipboard_done), ==, 1);
    g_assert_null(p.keyboard);
    g_assert_false(p.clipboard_enabled);
    callback_done = FALSE;
    expected_denial = TRUE;
    g_atomic_int_set(&deny, 1);
    portal_enable_keyboard(&p, complete, NULL);
    wait_for(&callback_done);
    g_assert_null(p.keyboard);
    g_assert_false(p.keyboard_pending);
    portal_clear(&p);
    g_autofree char *token_path = g_build_filename(config, "keyboard-token", NULL);
    g_unlink(token_path);
    g_rmdir(config);
    g_main_loop_quit(service_loop);
    g_thread_join(thread);
    g_free(keyboard_session);
    g_free(shortcut_session);
    g_test_dbus_down(dbus);
    g_object_unref(dbus);
    g_print("PASS: portal registration, early Response race, shortcut signals, keyboard-only "
            "permission, Korean clipboard FD transfer, revocation, denial\n");
    return 0;
}
