#pragma once
#include <gtk/gtk.h>

// GDK exposes the compositor's current XKB map, including user remaps.
// Portal keycodes are evdev codes; GDK/XKB keycodes include an offset of eight.
static int input_keycode(int keysym) {
    GdkKeymapKey *keys = NULL;
    gint count = 0;
    GdkKeymap *map = gdk_keymap_get_for_display(gdk_display_get_default());
    int code = -1;
    if (gdk_keymap_get_entries_for_keyval(map, keysym, &keys, &count)) {
        for (int i = 0; i < count; i++) {
            if (keys[i].group == 0 && keys[i].level == 0 && keys[i].keycode >= 8) {
                code = keys[i].keycode - 8;
                break;
            }
        }
    }
    g_free(keys);
    return code;
}
