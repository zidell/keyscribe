#include "overlay.h"
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <pango/pangocairo.h>
static GtkWidget *window;
static char *title;
static double volume, phase;
static gboolean recording, time_warning;
static char *last_position;
void overlay_init(Overlay *overlay) {
    overlay->executable = g_file_read_link("/proc/self/exe", NULL);
}
void overlay_update(Overlay *overlay, const char *position, const char *text, double level,
                    gboolean warning) {
    if (!overlay->process && g_str_equal(position, "hidden"))
        return;
    if (!overlay->process) {
        GSubprocessLauncher *launcher = g_subprocess_launcher_new(
            G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
            G_SUBPROCESS_FLAGS_STDERR_SILENCE);
        // XWayland permits positioning an unfocused native popup on GNOME/Wayland.
        g_subprocess_launcher_setenv(launcher, "GDK_BACKEND", "x11", TRUE);
        overlay->process =
            g_subprocess_launcher_spawn(launcher, NULL, overlay->executable, "--overlay", NULL);
        g_object_unref(launcher);
        if (!overlay->process)
            return;
    }
    g_autofree char *line = g_strdup_printf("%s\t%.4f\t%d\t%s\n", position, level, warning, text);
    GOutputStream *stream = g_subprocess_get_stdin_pipe(overlay->process);
    g_autoptr(GError) error = NULL;
    if (!g_output_stream_write_all(stream, line, strlen(line), NULL, NULL, &error))
        g_clear_object(&overlay->process);
}
void overlay_close(Overlay *overlay) {
    if (overlay->process) {
        g_output_stream_close(g_subprocess_get_stdin_pipe(overlay->process), NULL, NULL);
        g_clear_object(&overlay->process);
    }
    g_clear_pointer(&overlay->executable, g_free);
}
static gboolean input(GIOChannel *channel, GIOCondition condition, void *user) {
    (void)user;
    if (condition & (G_IO_HUP | G_IO_ERR)) {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    char *line = NULL;
    gsize length;
    if (g_io_channel_read_line(channel, &line, &length, NULL, NULL) != G_IO_STATUS_NORMAL) {
        g_free(line);
        return G_SOURCE_CONTINUE;
    }
    g_auto(GStrv) parts = g_strsplit(line, "\t", 4);
    g_free(line);
    if (!parts[0] || !parts[1] || !parts[2] || !parts[3])
        return G_SOURCE_CONTINUE;
    g_strchomp(parts[3]);
    if (g_str_equal(parts[0], "hidden")) {
        gtk_widget_hide(window);
        return G_SOURCE_CONTINUE;
    }
    g_free(title);
    title = g_strdup(parts[3]);
    time_warning = atoi(parts[2]) != 0;
    volume = CLAMP(g_ascii_strtod(parts[1], NULL), 0, 1);
    recording = g_str_has_prefix(title, "녹음 중");
    gtk_widget_queue_draw(window);
    gboolean reposition = !gtk_widget_get_visible(window) || g_strcmp0(last_position, parts[0]);
    g_free(last_position);
    last_position = g_strdup(parts[0]);
    if (!reposition)
        return G_SOURCE_CONTINUE;
    GdkDisplay *display = gdk_display_get_default();
    GdkSeat *seat = gdk_display_get_default_seat(display);
    gint x = 0, y = 0;
    gdk_device_get_position(gdk_seat_get_pointer(seat), NULL, &x, &y);
    GdkMonitor *monitor = gdk_display_get_monitor_at_point(display, x, y);
    GdkRectangle area;
    gdk_monitor_get_workarea(monitor, &area);
    x = area.x + (area.width - 260) / 2;
    y = area.y + area.height - 92;
    if (strstr(parts[0], "left"))
        x = area.x + 40;
    else if (strstr(parts[0], "right"))
        x = area.x + area.width - 300;
    if (g_str_has_prefix(parts[0], "top"))
        y = area.y + 40;
    else if (g_str_equal(parts[0], "center"))
        y = area.y + (area.height - 52) / 2;
    gtk_window_move(GTK_WINDOW(window), x, y);
    gtk_widget_show_all(window);
    cairo_region_t *empty = cairo_region_create();
    gdk_window_input_shape_combine_region(gtk_widget_get_window(window), empty, 0, 0);
    cairo_region_destroy(empty);
    return G_SOURCE_CONTINUE;
}
// Geometry, colours, and waveform are ported from native/windows/src/overlay.rs
// and native/macos/Sources/KeyScribe/RecordingOverlay.swift.
static void pill(cairo_t *cr, double x, double y, double width, double height) {
    double radius = MIN(width, height) / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, x + radius, y + height - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}
static void draw_text(cairo_t *cr, const char *text, int x, int y, double red, double green,
                      double blue) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string("Sans 10.5");
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    if (recording && g_str_has_prefix(text, "녹음 중 ")) {
        const char *time = strchr(text, '(');
        if (time) {
            PangoAttrList *attributes = pango_attr_list_new();
            PangoAttribute *color = pango_attr_foreground_new(time_warning ? 0xffff : 0x8d8d,
                                                              time_warning ? 0x5959 : 0x8d8d,
                                                              time_warning ? 0x5959 : 0x8d8d);
            color->start_index = time - text;
            color->end_index = strlen(text);
            pango_attr_list_insert(attributes, color);
            pango_layout_set_attributes(layout, attributes);
            pango_attr_list_unref(attributes);
        }
    }
    pango_layout_set_width(layout, 165 * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    cairo_set_source_rgb(cr, red, green, blue);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
}
static gboolean draw(GtkWidget *widget, cairo_t *cr, void *user) {
    (void)widget;
    (void)user;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.9);
    pill(cr, 0, 0, 260, 52);
    cairo_fill(cr);
    if (recording) {
        cairo_set_source_rgb(cr, 1, 0.28, 0.28);
        cairo_arc(cr, 25, 25, 6, 0, 2 * G_PI);
        cairo_fill(cr);
    } else if (title && g_str_has_prefix(title, "변환")) {
        const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧"};
        draw_text(cr, frames[((int)phase) % 8], 18, 16, 0.69, 0.69, 0.69);
    }
    draw_text(cr, title ? title : "",
              recording || (title && g_str_has_prefix(title, "변환")) ? 40 : 20, 16, 1, 1, 1);
    {
        cairo_set_source_rgb(cr, 1, recording ? 0.28 : 1, recording ? 0.28 : 1);
        for (int i = 0; i < 5; i++) {
            double wave = 0.5 + 0.5 * sin(phase + i * 1.3);
            double strength = volume * (0.5 + 0.5 * wave) + (1 - volume) * 0.12 * wave;
            double height = 4 + strength * 24;
            pill(cr, 210 + i * 7, (52 - height) / 2, 3, height);
            cairo_fill(cr);
        }
    }
    return TRUE;
}
static gboolean tick(void *user) {
    (void)user;
    phase += 0.4;
    if (!recording)
        volume *= 0.88;
    if (gtk_widget_get_visible(window))
        gtk_widget_queue_draw(window);
    return G_SOURCE_CONTINUE;
}
static void session_xauthority(void) {
    // GUI launchers normally inherit this. Source/dev shells may retain an old
    // XAUTHORITY after GNOME restarts; use the current local Mutter session.
    const char *display = g_getenv("DISPLAY");
    if (!display || display[0] != ':')
        return;
    g_autoptr(GDir) directory = g_dir_open(g_get_user_runtime_dir(), 0, NULL);
    if (!directory)
        return;
    const char *name;
    while ((name = g_dir_read_name(directory)))
        if (g_str_has_prefix(name, ".mutter-Xwaylandauth.")) {
            g_autofree char *path = g_build_filename(g_get_user_runtime_dir(), name, NULL);
            g_setenv("XAUTHORITY", path, TRUE);
            break;
        }
}
int overlay_run(void) {
    session_xauthority();
    if (!gtk_init_check(NULL, NULL))
        return 1;
    window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_title(GTK_WINDOW(window), "KeyScribe widget");
    GtkWidget *canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(canvas, 260, 52);
    gtk_container_add(GTK_CONTAINER(window), canvas);
    gtk_window_set_default_size(GTK_WINDOW(window), 260, 52);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_widget_set_app_paintable(window, TRUE);
    GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(window));
    if (visual)
        gtk_widget_set_visual(window, visual);
    g_signal_connect(window, "draw", G_CALLBACK(draw), NULL);
    g_timeout_add(50, tick, NULL);
    GIOChannel *channel = g_io_channel_unix_new(STDIN_FILENO);
    g_io_channel_set_close_on_unref(channel, FALSE);
    g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR, input, NULL);
    gtk_main();
    g_io_channel_unref(channel);
    g_free(title);
    g_free(last_position);
    return 0;
}
