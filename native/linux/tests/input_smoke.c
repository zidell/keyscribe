#include <gtk/gtk.h>
#include "portal.h"
#include "input.h"

static Portal portal;
static GtkWidget *window, *entry;
static int stage, submitted, result = 1;
static const char *sample = "한글 자동 입력 검사";
static void event(const char *id, gboolean pressed, void *user) {
    (void)id; (void)pressed; (void)user;
}
static void activated(GtkEntry *widget, void *user) {
    (void)widget; (void)user;
    submitted++;
}
static gboolean stop(void *user) {
    (void)user;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}
static void destroyed(GtkWidget *widget, void *user) {
    (void)widget; (void)user;
    if (gtk_main_level())
        gtk_main_quit();
}
static gboolean key(int symbol, gboolean down) {
    g_autoptr(GError) error = NULL;
    if (portal_key(&portal, symbol, down, &error))
        return TRUE;
    g_printerr("Input test failed: %s\n", error->message);
    gtk_main_quit();
    return FALSE;
}
static gboolean paste(void *user) {
    (void)user;
    // Never inject into the user's other windows, including this conversation.
    if (!gtk_window_is_active(GTK_WINDOW(window)) || !gtk_widget_has_focus(entry)) {
        g_printerr("Input test stopped: the private test entry does not have focus\n");
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    if (stage == 0) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), sample, -1);
        g_autoptr(GError) error = NULL;
        if (!portal_set_text(&portal, sample, &error)) {
            g_printerr("Clipboard test failed: %s\n", error->message);
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
    } else if (stage == 1) {
        if (!key(GDK_KEY_Control_L, TRUE) || !key(GDK_KEY_v, TRUE)) {
            key(GDK_KEY_v, FALSE);
            key(GDK_KEY_Control_L, FALSE);
            return G_SOURCE_REMOVE;
        }
    } else if (stage == 2) {
        gboolean released_v = key(GDK_KEY_v, FALSE);
        gboolean released_ctrl = key(GDK_KEY_Control_L, FALSE);
        if (!released_v || !released_ctrl)
            return G_SOURCE_REMOVE;
    } else if (stage == 3) {
        if (!key(GDK_KEY_Return, TRUE))
            return G_SOURCE_REMOVE;
    } else if (stage == 4) {
        if (!key(GDK_KEY_Return, FALSE))
            return G_SOURCE_REMOVE;
    } else {
        if (g_str_equal(gtk_entry_get_text(GTK_ENTRY(entry)), sample) && submitted == 1) {
            result = 0;
            g_print("PASS: real desktop keyboard permission, Korean clipboard paste, Enter received once\n");
        } else {
            g_printerr("FAIL: private entry text matched=%d Enter count=%d clipboard=%d\n",
                g_str_equal(gtk_entry_get_text(GTK_ENTRY(entry)), sample), submitted,
                portal.clipboard_enabled);
        }
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    stage++;
    return G_SOURCE_CONTINUE;
}
static void ready(GVariant *values, const GError *error, void *user) {
    (void)values; (void)user;
    if (error) {
        g_printerr("Permission restoration failed: %s\n", error->message);
        gtk_main_quit();
        return;
    }
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
    g_timeout_add(300, paste, NULL);
}
int main(int argc, char **argv) {
    gboolean authorize = argc == 2 && g_str_equal(argv[1], "--authorize");
    gtk_init(&argc, &argv);
    g_autoptr(GError) error = NULL;
    if (!portal_init(&portal, event, NULL, &error)) {
        g_printerr("Portal unavailable: %s\n", error->message);
        return 1;
    }
    portal.keycode_for_keysym = input_keycode;
    g_autofree char *config = g_build_filename(g_get_user_config_dir(), "keyscribe", NULL);
    portal.keyboard_token_path = portal_keyboard_token_path(config);
    if (!g_file_get_contents(portal.keyboard_token_path, &portal.keyboard_restore_token, NULL, NULL) &&
        !authorize) {
        g_print("SKIP: grant KeyScribe keyboard permission first\n");
        portal_clear(&portal);
        return 77;
    }
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "KeyScribe 자동 입력 검증");
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 80);
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window), entry);
    g_signal_connect(entry, "activate", G_CALLBACK(activated), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(destroyed), NULL);
    gtk_widget_show_all(window);
    g_timeout_add_seconds(authorize ? 120 : 15, stop, NULL);
    portal_enable_keyboard(&portal, ready, NULL);
    gtk_main();
    portal_clear(&portal);
    gtk_widget_destroy(window);
    return result;
}
