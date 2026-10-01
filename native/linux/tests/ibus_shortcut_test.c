#include "../src/portal.c"

int main(void) {
    // A private bus and memory settings keep the user's live IBus untouched.
    g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
    g_autofree char *directory = g_dir_make_tmp("keyscribe-ibus-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_setenv("XDG_CONFIG_HOME", directory, TRUE);
    g_autoptr(GTestDBus) test_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(test_bus);
    g_autoptr(GDBusConnection) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", g_variant_new("(su)", "org.freedesktop.impl.portal.desktop.gnome", 0u),
        NULL, 0, 3000, NULL, NULL);
    g_assert_nonnull(reply);
    g_autoptr(GSettings) hangul = ibus_settings(0);
    g_autoptr(GSettings) hotkeys = ibus_settings(1);
    if (!hangul || !hotkeys) {
        g_print("SKIP: IBus schemas unavailable on this build host\n");
        return 0;
    }
    g_settings_set_string(hangul, "switch-keys", "Hangul,Shift+space");
    const char *original[] = {"Control+space", "Hangul", "Alt+grave", NULL};
    g_settings_set_strv(hotkeys, "trigger", original);
    Portal p = {.bus = bus, .preferred_trigger = "Hangul"};
    ibus_reserve(&p);
    g_autofree char *reserved = g_settings_get_string(hangul, "switch-keys");
    g_assert_cmpstr(reserved, ==, "Shift+space");
    g_auto(GStrv) triggers = g_settings_get_strv(hotkeys, "trigger");
    g_assert_cmpuint(g_strv_length(triggers), ==, 2);
    g_assert_cmpstr(triggers[0], ==, "Control+space");
    g_assert_cmpstr(triggers[1], ==, "Alt+grave");
    g_autofree char *path = ibus_backup_path();
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
    GStatBuf metadata;
    g_assert_cmpint(g_stat(path, &metadata), ==, 0);
    g_assert_cmpuint(metadata.st_mode & 0777, ==, 0600);
    // Repeated registration must retain the original recovery settings.
    ibus_reserve(&p);
    ibus_restore(&p);
    g_autofree char *restored = g_settings_get_string(hangul, "switch-keys");
    g_assert_cmpstr(restored, ==, "Hangul,Shift+space");
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    g_auto(GStrv) restored_triggers = g_settings_get_strv(hotkeys, "trigger");
    g_assert_cmpuint(g_strv_length(restored_triggers), ==, 3);
    g_assert_cmpstr(restored_triggers[1], ==, "Hangul");
    // Recover an interrupted prior process, including a changed shortcut.
    ibus_reserve(&p);
    Portal next = {.bus = bus, .preferred_trigger = "CTRL+ALT+space"};
    ibus_reserve(&next);
    g_autofree char *recovered = g_settings_get_string(hangul, "switch-keys");
    g_assert_cmpstr(recovered, ==, "Hangul,Shift+space");
    // An independent user preference edit takes precedence over restoration.
    ibus_reserve(&p);
    g_settings_set_string(hangul, "switch-keys", "Control+space");
    ibus_restore(&p);
    g_autofree char *edited = g_settings_get_string(hangul, "switch-keys");
    g_assert_cmpstr(edited, ==, "Control+space");
    g_autofree char *config = g_path_get_dirname(path);
    g_rmdir(config);
    g_rmdir(directory);
    g_print("PASS: Hangul reservation, other language keys preserved, private backup, "
            "rebind, restore, crash recovery, user edits preserved\n");
    return 0;
}
