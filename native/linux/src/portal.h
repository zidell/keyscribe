#pragma once
#include <gio/gio.h>
typedef void (*PortalResult)(GVariant *result, const GError *error, void *user);
typedef void (*ShortcutEvent)(const char *id, gboolean pressed, void *user);
typedef struct {
    GDBusConnection *bus;
    char *shortcuts, *keyboard, *preferred_trigger;
    char *keyboard_token_path, *keyboard_restore_token;
    guint shortcut_subscription, closed_subscription;
    ShortcutEvent event;
    int (*keycode_for_keysym)(int keysym);
    void (*trace)(const char *event);
    void (*keyboard_closed)(void *user);
    void *user;
    gboolean keyboard_pending, shortcuts_pending, clipboard_enabled;
    gboolean ibus_managed;
    gboolean escape_active;
    guint escape_registration;
    char *clipboard_text;
    guint clipboard_subscription;
} Portal;
gboolean portal_init(Portal *p, ShortcutEvent event, void *user, GError **error);
void portal_clear(Portal *p);
void portal_bind(Portal *p, PortalResult callback, void *user);
void portal_pause_shortcuts(Portal *p);
char *portal_keyboard_token_path(const char *config_dir);
void portal_enable_keyboard(Portal *p, PortalResult callback, void *user);
gboolean portal_key(Portal *p, int keysym, gboolean pressed, GError **error);
gboolean portal_set_text(Portal *p, const char *text, GError **error);

void portal_capture_escape(Portal *p, gboolean active);
