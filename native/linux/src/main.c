#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include "portal.h"
#include "output.h"
#include "overlay.h"
#include "sounds.h"
#ifndef KEYSCRIBE_VERSION
#define KEYSCRIBE_VERSION "0.1.1"
#endif
#include <json-glib/json-glib.h>
#include <libintl.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <curl/curl.h>
#include <glib/gstdio.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

typedef enum { IDLE, CONNECTING, RECORDING, TRANSCRIBING } State;
typedef struct {
    GtkApplication *application;
    GtkWidget *window, *status, *level, *record_button, *result, *key, *language, *model, *refresh,
        *terms, *rules, *hold, *send, *limit, *retention, *mute, *no_verbatim, *volume, *position,
        *shortcut_choice;
    AppIndicator *indicator;
    GtkWidget *menu_status;
    Settings settings;
    Portal portal;
    State state;
    char *config_dir, *logs_dir, *wav;
    FILE *file;
    guint32 bytes;
    gint64 started;
    guint timer;
    gboolean from_shortcut;
    gboolean trigger_down;
    GCancellable *cancel, *models_cancel;
    guint model_timer, model_generation;
    gulong model_changed_handler;
    Provider model_provider;
    char *draft_models[4];
    Output output;
    Overlay overlay;
    GQueue jobs;
    GPtrArray *paste_steps;
    guint paste_index, paste_timer;
    double input_level;
    gboolean pasting_text, paste_auto_send;
    gboolean closing;
    gboolean show_on_start;
    char *tray_icons[3];
    pa_glib_mainloop *audio_loop;
    pa_context *context;
    pa_stream *stream;
} App;
static App app;
static void stop_recording(gboolean cancel);
static void start_recording(gboolean shortcut);
static void process_results(void);
static void cancel_jobs(void);
static void discard_settings(GtkWidget *w, void *user);
static void keyboard_done(GVariant *values, const GError *error, void *user);
static void update_overlay(void) {
    if (app.closing)
        return;
    g_autofree char *title = NULL;
    if (app.state == RECORDING) {
        int seconds = (g_get_monotonic_time() - app.started) / G_USEC_PER_SEC;
        title = g_strdup_printf("녹음 중 (%02d:%02d)", seconds / 60, seconds % 60);
    } else
        title = g_strdup(app.state == CONNECTING     ? "마이크 연결 중"
                         : app.state == TRANSCRIBING ? "변환 중..."
                                                     : "");
    overlay_update(&app.overlay, app.state == IDLE ? "hidden" : app.settings.overlay_position,
                   title, app.input_level,
                   app.state == RECORDING &&
                       (g_get_monotonic_time() - app.started) / G_USEC_PER_SEC >=
                           app.settings.limit_minutes * 60 - 60);
}
static void set_status(const char *message) {
    gtk_label_set_text(GTK_LABEL(app.status), message);
    gtk_menu_item_set_label(GTK_MENU_ITEM(app.menu_status), message);
    update_overlay();
}
static void set_state(State state) {
    if (state == IDLE && !g_queue_is_empty(&app.jobs))
        state = TRANSCRIBING;
    app.state = state;
    portal_capture_escape(&app.portal, state != IDLE);
    g_autofree char *event =
        g_strdup_printf("state=%d pending=%u", state, g_queue_get_length(&app.jobs));
    debug_log(event);
    app_indicator_set_icon_full(app.indicator,
                                app.tray_icons[state == RECORDING      ? 1
                                               : state == TRANSCRIBING ? 2
                                                                       : 0]
                                    ? app.tray_icons[state == RECORDING      ? 1
                                                     : state == TRANSCRIBING ? 2
                                                                             : 0]
                                    : "audio-input-microphone",
                                "KeyScribe");
    gtk_button_set_label(GTK_BUTTON(app.record_button),
                         state == CONNECTING || state == RECORDING ? "녹음 종료" : "녹음 시작");
    gtk_widget_set_sensitive(app.record_button, TRUE);
    gtk_widget_set_sensitive(app.key, state == IDLE);
    gtk_widget_set_sensitive(app.model, state == IDLE && provider_from_key(gtk_entry_get_text(
                                                             GTK_ENTRY(app.key))) != PROVIDER_NONE);
    gtk_widget_set_sensitive(app.refresh, state == IDLE);
}
static void show_window(GtkMenuItem *item, void *user) {
    (void)item;
    (void)user;
    gtk_window_present(GTK_WINDOW(app.window));
}
static gboolean hide_window(GtkWidget *w, GdkEvent *e, void *user) {
    (void)e;
    (void)user;
    discard_settings(w, NULL);
    gtk_widget_hide(w);
    return TRUE;
}
static void open_logs(GtkMenuItem *item, void *user) {
    (void)item;
    (void)user;
    g_autofree char *uri = g_filename_to_uri(app.logs_dir, NULL, NULL);
    g_autoptr(GError) error = NULL;
    if (!g_app_info_launch_default_for_uri(uri, NULL, &error))
        set_status(error->message);
}
static void quit(GtkMenuItem *item, void *user) {
    (void)item;
    (void)user;
    g_application_quit(G_APPLICATION(app.application));
}
static void restart(GtkMenuItem *item, void *user) {
    (void)item;
    (void)user;
    g_autofree char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (exe) {
        char pid[32];
        g_snprintf(pid, sizeof(pid), "%d", (int)getpid());
        const char *argv[] = {exe, "--restart-delayed", pid, NULL};
        g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL);
        g_application_quit(G_APPLICATION(app.application));
    }
}
static void toggle(GtkWidget *button, void *user) {
    (void)button;
    (void)user;
    if (app.state == RECORDING || app.state == CONNECTING)
        stop_recording(FALSE);
    else if (app.state == IDLE || app.state == TRANSCRIBING)
        start_recording(FALSE);
}
static void cancel_action(GtkWidget *button, void *user) {
    (void)button;
    (void)user;
    if (app.state == RECORDING || app.state == CONNECTING)
        stop_recording(TRUE);
    cancel_jobs();
    if (app.state != RECORDING && app.state != CONNECTING)
        set_status("취소했습니다. 녹음 원본은 보관됩니다.");
}
static gboolean key_press(GtkWidget *w, GdkEventKey *event, void *user) {
    (void)w;
    (void)user;
    if (event->keyval == GDK_KEY_Escape) {
        cancel_action(NULL, NULL);
        return TRUE;
    }
    return FALSE;
}
static void shortcut(const char *id, gboolean pressed, void *user) {
    (void)user;
    if (g_str_equal(id, "cancel")) {
        if (pressed)
            cancel_action(NULL, NULL);
        return;
    }
    if (!g_str_equal(id, "record"))
        return;
    if (pressed && app.trigger_down)
        return;
    app.trigger_down = pressed;
    debug_log(pressed ? "record shortcut pressed" : "record shortcut released");
    if (app.settings.hold) {
        if (pressed && (app.state == IDLE || app.state == TRANSCRIBING))
            start_recording(TRUE);
        else if (!pressed && app.from_shortcut &&
                 (app.state == RECORDING || app.state == CONNECTING))
            stop_recording(FALSE);
    } else if (pressed) {
        if (app.state == IDLE || app.state == TRANSCRIBING)
            start_recording(TRUE);
        else if (app.state == RECORDING || app.state == CONNECTING)
            stop_recording(FALSE);
    }
}
static void binding_done(GVariant *values, const GError *error, void *user) {
    (void)user;
    if (error) {
        set_status(error->message);
        return;
    }
    app.settings.shortcuts_enabled = TRUE;
    app.trigger_down = FALSE;
    settings_save(&app.settings, app.config_dir, NULL);
    g_autofree char *registered = g_strdup_printf(
        "record shortcut registration response trigger=%s session=%s hold=%d",
        app.settings.shortcut, app.portal.shortcuts ? app.portal.shortcuts : "none",
        app.settings.hold);
    debug_log(registered);
    GString *message = g_string_new("전역 단축키: ");
    g_autoptr(GVariant) items =
        g_variant_lookup_value(values, "shortcuts", G_VARIANT_TYPE("a(sa{sv})"));
    if (items)
        for (gsize i = 0; i < g_variant_n_children(items); i++) {
            g_autoptr(GVariant) item = g_variant_get_child_value(items, i);
            const char *id;
            GVariant *opts;
            g_variant_get(item, "(&s@a{sv})", &id, &opts);
            const char *trigger = NULL;
            if (g_str_equal(id, "record") &&
                g_variant_lookup(opts, "trigger_description", "&s", &trigger))
                g_string_append(message, trigger);
            g_variant_unref(opts);
        }
    set_status(message->str);
    g_string_free(message, TRUE);
}
static void bind_shortcuts(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    g_free(app.portal.preferred_trigger);
    const char *selected = app.shortcut_choice
                               ? gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.shortcut_choice))
                               : NULL;
    app.portal.preferred_trigger = g_strdup(selected ? selected : app.settings.shortcut);
    if (selected && g_strcmp0(selected, app.settings.shortcut)) {
        g_free(app.settings.shortcut);
        app.settings.shortcut = g_strdup(selected);
        settings_save(&app.settings, app.config_dir, NULL);
    }
    set_status("시스템 단축키 창에서 등록을 완료하세요. 완료 전에는 녹음 키가 동작하지 않습니다.");
    portal_bind(&app.portal, binding_done, NULL);
}
static gboolean capture_shortcut(GtkWidget *dialog, GdkEventKey *event, void *user) {
    GtkWidget *label = user;
    g_autofree char *diagnostic = g_strdup_printf(
        "shortcut capture key=%s code=%u modifier=%u state=%u",
        gdk_keyval_name(event->keyval) ? gdk_keyval_name(event->keyval) : "unknown",
        event->hardware_keycode, event->is_modifier, event->state);
    debug_log(diagnostic);
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(dialog);
        return TRUE;
    }
    (void)label;
    guint keyval = gdk_keyval_to_lower(event->keyval);
    GdkModifierType mods = event->is_modifier ? 0 :
        event->state & gtk_accelerator_get_default_mod_mask();
    if (!event->is_modifier && !gtk_accelerator_valid(keyval, mods))
        return TRUE;
    const char *name = gdk_keyval_name(keyval);
    if (!name)
        return TRUE;
    g_autofree char *trigger = g_strconcat(mods & GDK_CONTROL_MASK ? "CTRL+" : "",
                                         mods & GDK_MOD1_MASK ? "ALT+" : "",
                                         mods & GDK_SHIFT_MASK ? "SHIFT+" : "",
                                         mods & GDK_SUPER_MASK ? "LOGO+" : "", name, NULL);
    g_autofree char *display = gtk_accelerator_get_label(keyval, mods);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.shortcut_choice), trigger)) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.shortcut_choice), trigger, display);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.shortcut_choice), trigger);
    }
    gtk_widget_destroy(dialog);
    return TRUE;
}
static void capture_closed(GtkWidget *dialog, void *user) {
    (void)dialog;
    (void)user;
    if (app.settings.shortcuts_enabled && !app.closing) {
        g_free(app.portal.preferred_trigger);
        app.portal.preferred_trigger = g_strdup(app.settings.shortcut);
        portal_bind(&app.portal, binding_done, NULL);
    }
}
static void choose_shortcut(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    portal_pause_shortcuts(&app.portal);
    app.trigger_down = FALSE;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("녹음 키 지정", GTK_WINDOW(app.window),
                                                  GTK_DIALOG_MODAL, "취소", GTK_RESPONSE_CANCEL,
                                                  NULL);
    GtkWidget *label = gtk_label_new("녹음에 사용할 키를 누르세요. Esc는 취소입니다.");
    gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                                   24);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);
    g_signal_connect(dialog, "key-press-event", G_CALLBACK(capture_shortcut), label);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    g_signal_connect(dialog, "destroy", G_CALLBACK(capture_closed), NULL);
    gtk_widget_show_all(dialog);
}
static void application_action(GSimpleAction *action, GVariant *parameter, void *user) {
    (void)parameter;
    (void)user;
    const char *name = g_action_get_name(G_ACTION(action));
    if (g_str_equal(name, "cancel")) {
        cancel_action(NULL, NULL);
    } else if (g_str_equal(name, "enable-keyboard")) {
        portal_enable_keyboard(&app.portal, keyboard_done, NULL);
    } else if (app.window) {
        show_window(NULL, NULL);
        if (g_str_equal(name, "choose-shortcut"))
            choose_shortcut(NULL, NULL);
    }
}
static void close_settings(GtkWidget *w, void *user) {
    discard_settings(w, user);
    gtk_widget_hide(app.window);
}
static void keyboard_done(GVariant *values, const GError *error, void *user) {
    (void)values;
    (void)user;
    debug_log(error ? "automatic input keyboard permission failed" :
                     "automatic input keyboard permission ready");
    set_status(error
                   ? error->message
                   : "자동 붙여넣기 사용 가능. 입력할 앱에 커서를 두고 전역 단축키로 녹음하세요.");
    if (error)
        debug_log(error->message);
    if (!app.closing)
        process_results();
}
static void keyboard_closed(void *user) {
    (void)user;
    if (app.closing)
        return;
    app.pasting_text = FALSE;
    set_status("자동 붙여넣기 연결이 끊겨 복구 중입니다…");
    // Restore only a previously granted permission. The portal still decides
    // whether the saved grant is valid and handles any required consent.
    if (app.portal.keyboard_restore_token && *app.portal.keyboard_restore_token) {
        debug_log("automatic input keyboard session restoring after close");
        portal_enable_keyboard(&app.portal, keyboard_done, NULL);
    }
}
static void enable_keyboard(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    portal_enable_keyboard(&app.portal, keyboard_done, NULL);
}
static char *view_text(GtkWidget *view) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(b, &start, &end);
    return gtk_text_buffer_get_text(b, &start, &end, FALSE);
}
static void copy_result(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    g_autofree char *text = view_text(app.result);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    set_status("결과를 복사했습니다. Ctrl+V로 붙여넣으세요.");
}
static gboolean send_key(int symbol, gboolean pressed) {
    g_autoptr(GError) error = NULL;
    if (!portal_key(&app.portal, symbol, pressed, &error)) {
        set_status(error->message);
        return FALSE;
    }
    return TRUE;
}
typedef struct {
    Settings settings;
    char *path, *text;
    GError *error;
    GCancellable *cancel;
    gboolean paste, ready;
    guint references;
} Job;
static void job_free(void *data) {
    Job *j = data;
    if (--j->references)
        return;
    settings_clear(&j->settings);
    g_free(j->path);
    g_free(j->text);
    g_clear_error(&j->error);
    g_clear_object(&j->cancel);
    g_free(j);
}
static void finish_paste(gboolean success) {
    debug_log(success ? "automatic input completed" : "automatic input failed");
    g_clear_pointer(&app.paste_steps, g_ptr_array_unref);
    app.paste_timer = 0;
    app.paste_index = 0;
    app.pasting_text = FALSE;
    if (app.state != RECORDING && app.state != CONNECTING) {
        set_state(IDLE);
        if (success)
            set_status("입력 완료");
        process_results();
    }
}
static gboolean paste_tick(void *user) {
    (void)user;
    if (app.state == RECORDING || app.state == CONNECTING)
        return G_SOURCE_CONTINUE;
    if (app.portal.keyboard_pending)
        return G_SOURCE_CONTINUE;
    if (!app.portal.keyboard) {
        set_status("키보드 권한이 해제되었습니다. 결과를 복사해 붙여넣으세요.");
        finish_paste(FALSE);
        return G_SOURCE_REMOVE;
    }
    if (app.paste_index >= app.paste_steps->len) {
        finish_paste(TRUE);
        return G_SOURCE_REMOVE;
    }
    PasteSegment *segment = g_ptr_array_index(app.paste_steps, app.paste_index);
    gboolean ok = TRUE;
    if (segment->text && !app.pasting_text) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), segment->text, -1);
        g_autoptr(GError) error = NULL;
        if (!portal_set_text(&app.portal, segment->text, &error)) {
            set_status(error->message);
            finish_paste(FALSE);
            return G_SOURCE_REMOVE;
        }
        app.pasting_text = TRUE;
        return G_SOURCE_CONTINUE;
    }
    if (segment->text) {
        guint keys[] = {GDK_KEY_Control_L, GDK_KEY_v};
        for (guint i = 0; i < 2 && ok; i++)
            ok = send_key(keys[i], TRUE);
        for (int i = 1; i >= 0; i--)
            if (!send_key(keys[i], FALSE))
                ok = FALSE;
        app.pasting_text = FALSE;
    } else {
        for (guint i = 0; i < segment->n_keys && ok; i++)
            ok = send_key(segment->keys[i], TRUE);
        for (int i = (int)segment->n_keys - 1; i >= 0; i--)
            if (!send_key(segment->keys[i], FALSE))
                ok = FALSE;
    }
    ++app.paste_index;
    if (!ok) {
        finish_paste(FALSE);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
static void deliver(Job *j) {
    if (j->error) {
        set_status(g_error_matches(j->error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
                       ? "취소했습니다. 녹음 원본은 보관됩니다."
                       : j->error->message);
        return;
    }
    g_autofree char *text = apply_replacements(j->text, j->settings.replacements);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.result)), text, -1);
    if (!*text) {
        set_status("인식된 음성이 없습니다");
        return;
    }
    if (j->paste && app.portal.keyboard) {
        debug_log(j->settings.auto_send ? "automatic input queued with Enter" :
                                         "automatic input queued");
        g_autofree char *with_enter =
            g_strconcat(text, j->settings.auto_send ? "[enter]" : "", NULL);
        app.paste_steps = paste_segments(with_enter);
        app.paste_index = 0;
        app.pasting_text = FALSE;
        app.paste_timer = g_timeout_add(150, paste_tick, NULL);
    } else {
        debug_log(j->paste ? "delivery copied only: keyboard permission missing" :
                             "delivery copied only: recording started from settings");
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
        set_status(j->paste
                       ? "자동 붙여넣기 권한이 없습니다. 권한을 허용하거나 Ctrl+V로 붙여넣으세요."
                       : "전사 완료. 결과를 복사하고 Ctrl+V로 붙여넣으세요.");
        if (j->paste)
            show_window(NULL, NULL);
    }
}
static void process_results(void) {
    if (app.state == RECORDING || app.state == CONNECTING || app.paste_steps ||
        app.portal.keyboard_pending)
        return;
    while (!g_queue_is_empty(&app.jobs)) {
        Job *j = g_queue_peek_head(&app.jobs);
        if (!j->ready)
            return;
        g_queue_pop_head(&app.jobs);
        set_state(IDLE);
        if (!g_cancellable_is_cancelled(j->cancel))
            deliver(j);
        job_free(j);
        if (app.paste_steps)
            return;
    }
}
static void cancel_jobs(void) {
    for (GList *node = app.jobs.head; node; node = node->next)
        g_cancellable_cancel(((Job *)node->data)->cancel);
    if (app.paste_timer) {
        g_source_remove(app.paste_timer);
        app.paste_timer = 0;
    }
    g_clear_pointer(&app.paste_steps, g_ptr_array_unref);
    process_results();
}
static void transcribe_worker(GTask *task, void *source, void *data, GCancellable *cancel) {
    (void)source;
    Job *j = data;
    GError *error = NULL;
    char *text = transcribe(j->path, &j->settings, cancel, &error);
    if (text)
        g_task_return_pointer(task, text, g_free);
    else
        g_task_return_error(task, error);
}
static void transcribed(GObject *obj, GAsyncResult *result, void *user) {
    (void)obj;
    (void)user;
    GTask *task = G_TASK(result);
    Job *j = g_task_get_task_data(task);
    j->text = g_task_propagate_pointer(task, &j->error);
    j->ready = TRUE;
    debug_log(j->error ? "transcription finished error" : "transcription finished success");
    process_results();
}
static void chime_done(GObject *obj, GAsyncResult *result, void *user) {
    (void)obj;
    g_task_propagate_boolean(G_TASK(result), NULL);
    if ((gint64)(gintptr)user == app.started &&
        (app.state == RECORDING || app.state == CONNECTING) && app.settings.mute_during_recording)
        output_mute(&app.output);
}
static void audio_clear(void) {
    if (app.stream) {
        pa_stream_set_state_callback(app.stream, NULL, NULL);
        pa_stream_set_read_callback(app.stream, NULL, NULL);
        pa_stream_disconnect(app.stream);
        pa_stream_unref(app.stream);
        app.stream = NULL;
    }
    if (app.context) {
        pa_context_set_state_callback(app.context, NULL, NULL);
        pa_context_disconnect(app.context);
        pa_context_unref(app.context);
        app.context = NULL;
    }
}
static gboolean recording_tick(void *user) {
    (void)user;
    if (app.state != RECORDING && app.state != CONNECTING)
        return G_SOURCE_REMOVE;
    int seconds = (g_get_monotonic_time() - app.started) / G_USEC_PER_SEC;
    if (seconds >= app.settings.limit_minutes * 60) {
        app.timer = 0;
        stop_recording(FALSE);
        sound_play(TRUE, 100, NULL, NULL);
        return G_SOURCE_REMOVE;
    }
    if (app.state == RECORDING) {
        g_autofree char *message = g_strdup_printf(
            "녹음 중 %02d:%02d — %s", seconds / 60, seconds % 60,
            app.settings.hold && app.from_shortcut ? "단축키를 놓으면 전사" : "다시 누르면 전사");
        set_status(message);
    }
    return G_SOURCE_CONTINUE;
}
static void audio_read(pa_stream *stream, size_t available, void *user) {
    (void)available;
    (void)user;
    const void *data;
    size_t size;
    if (pa_stream_peek(stream, &data, &size) < 0) {
        stop_recording(TRUE);
        set_status("마이크 데이터를 읽지 못했습니다");
        return;
    }
    gboolean ok = TRUE;
    if (data && size && app.file) {
        ok = fwrite(data, 1, size, app.file) == size;
        if (ok)
            app.bytes += size;
        double sum = 0;
        const unsigned char *pcm = data;
        for (size_t i = 0; i + 1 < size; i += 2) {
            gint16 sample = (gint16)((guint16)pcm[i] | ((guint16)pcm[i + 1] << 8));
            double n = sample / 32768.0;
            sum += n * n;
        }
        app.input_level = size ? MIN(1.0, sqrt(sum / (size / 2)) * 8.0) : 0;
        gtk_level_bar_set_value(GTK_LEVEL_BAR(app.level), app.input_level);
        update_overlay();
    }
    pa_stream_drop(stream);
    if (!ok) {
        stop_recording(TRUE);
        set_status("녹음 저장 실패. 디스크 여유 공간을 확인하세요.");
    }
}
static void stream_state(pa_stream *stream, void *user) {
    (void)user;
    pa_stream_state_t state = pa_stream_get_state(stream);
    if (state == PA_STREAM_READY) {
        set_state(RECORDING);
        set_status("녹음 중…");
    } else if (state == PA_STREAM_FAILED) {
        stop_recording(TRUE);
        set_status("마이크 녹음 연결 실패. 우분투 소리 설정을 확인하세요.");
    }
}
static void context_state(pa_context *context, void *user) {
    (void)user;
    pa_context_state_t state = pa_context_get_state(context);
    if (state == PA_CONTEXT_READY) {
        pa_sample_spec spec = {PA_SAMPLE_S16LE, 16000, 1};
        app.stream = pa_stream_new(context, "KeyScribe microphone", &spec, NULL);
        if (!app.stream) {
            stop_recording(TRUE);
            set_status("녹음 스트림 생성 실패");
            return;
        }
        pa_stream_set_state_callback(app.stream, stream_state, NULL);
        pa_stream_set_read_callback(app.stream, audio_read, NULL);
        pa_buffer_attr attr = {.maxlength = (uint32_t)-1,
                               .tlength = (uint32_t)-1,
                               .prebuf = (uint32_t)-1,
                               .minreq = (uint32_t)-1,
                               .fragsize = 3200};
        if (pa_stream_connect_record(app.stream, NULL, &attr, PA_STREAM_ADJUST_LATENCY) < 0) {
            stop_recording(TRUE);
            set_status("마이크 연결 실패");
        }
    } else if (state == PA_CONTEXT_FAILED) {
        stop_recording(TRUE);
        set_status("오디오 서버 연결 실패. PipeWire 또는 PulseAudio가 실행 중인지 확인하세요.");
    }
}
static void start_recording(gboolean from_shortcut) {
    if (app.state == RECORDING || app.state == CONNECTING)
        return;
    if (!provider_from_key(app.settings.api_key)) {
        set_status("설정에서 API 키를 입력하고 저장하세요");
        if (!from_shortcut)
            show_window(NULL, NULL);
        return;
    }
    g_autofree char *stamp =
        g_strdup_printf("recording-%" G_GINT64_FORMAT "-XXXXXX.wav", g_get_real_time());
    g_free(app.wav);
    app.wav = g_build_filename(app.logs_dir, stamp, NULL);
    int fd = g_mkstemp_full(app.wav, O_RDWR, 0600);
    if (fd < 0) {
        set_status("녹음 파일을 만들 수 없습니다");
        return;
    }
    app.file = fdopen(fd, "w+b");
    if (!app.file) {
        close(fd);
        set_status("녹음 파일을 열 수 없습니다");
        return;
    }
    if (!wav_header(app.file, 0)) {
        fclose(app.file);
        app.file = NULL;
        set_status("녹음 헤더 저장 실패");
        return;
    }
    app.bytes = 0;
    app.started = g_get_monotonic_time();
    debug_log("recording started");
    if (app.settings.sound_volume > 0)
        sound_play(FALSE, app.settings.sound_volume, chime_done, (gpointer)(gintptr)app.started);
    else if (app.settings.mute_during_recording)
        output_mute(&app.output);
    app.from_shortcut = from_shortcut;
    set_state(CONNECTING);
    set_status("마이크 연결 중…");
    app.context = pa_context_new(pa_glib_mainloop_get_api(app.audio_loop), "KeyScribe");
    if (!app.context) {
        stop_recording(TRUE);
        set_status("오디오 초기화 실패");
        return;
    }
    pa_context_set_state_callback(app.context, context_state, NULL);
    app.timer = g_timeout_add_seconds(1, recording_tick, NULL);
    if (pa_context_connect(app.context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        stop_recording(TRUE);
        set_status("오디오 서버 연결 실패");
    }
}
static void stop_recording(gboolean cancel) {
    if (app.state != RECORDING && app.state != CONNECTING)
        return;
    audio_clear();
    output_restore(&app.output);
    app.input_level = 0;
    debug_log(cancel ? "recording cancelled" : "recording stopped");
    if (app.timer) {
        g_source_remove(app.timer);
        app.timer = 0;
    }
    gboolean ok = app.file && wav_header(app.file, app.bytes);
    if (app.file) {
        if (fclose(app.file) != 0)
            ok = FALSE;
        app.file = NULL;
    }
    gtk_level_bar_set_value(GTK_LEVEL_BAR(app.level), 0);
    set_state(IDLE);
    if (!ok) {
        set_status("녹음 파일 저장 실패");
        return;
    }
    if (cancel) {
        set_status("녹음을 취소했습니다. 원본은 보관됩니다.");
        process_results();
        return;
    }
    if (app.bytes < 9600) {
        set_status("녹음이 너무 짧습니다. 조금 더 길게 말하세요.");
        process_results();
        return;
    }
    Job *j = g_new0(Job, 1);
    settings_copy(&j->settings, &app.settings);
    j->path = g_strdup(app.wav);
    j->paste = app.from_shortcut;
    j->cancel = g_cancellable_new();
    j->references = 2;
    g_queue_push_tail(&app.jobs, j);
    GTask *task = g_task_new(NULL, j->cancel, transcribed, NULL);
    g_task_set_task_data(task, j, job_free);
    g_task_set_return_on_cancel(task, FALSE);
    set_state(TRANSCRIBING);
    set_status("음성을 전사하고 있습니다…");
    g_task_run_in_thread(task, transcribe_worker);
    g_object_unref(task);
}
typedef struct {
    char *key;
    guint generation;
} ModelJob;
static void model_job_free(void *user) {
    ModelJob *j = user;
    g_free(j->key);
    g_free(j);
}
static void selected_model(GtkComboBox *widget, void *user) {
    (void)user;
    const char *id = gtk_combo_box_get_active_id(widget);
    if (app.no_verbatim)
        gtk_widget_set_sensitive(app.no_verbatim, app.model_provider == PROVIDER_ELEVENLABS && id &&
                                                      (g_str_equal(id, "scribe_v2") ||
                                                       g_str_equal(id, "scribe_v2_medical")));
    if (id && app.model_provider) {
        g_free(app.draft_models[app.model_provider]);
        app.draft_models[app.model_provider] = g_strdup(id);
    }
}
static void populate_models(Provider p, char **names) {
    const char *chosen = app.draft_models[p] ? app.draft_models[p] : app.settings.models[p];
    g_autofree char *keep = g_strdup(chosen);
    g_signal_handler_block(app.model, app.model_changed_handler);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app.model));
    for (int i = 0; names[i]; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.model), names[i], names[i]);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.model), keep))
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.model), 0);
    g_signal_handler_unblock(app.model, app.model_changed_handler);
    selected_model(GTK_COMBO_BOX(app.model), NULL);
}
static void default_models(Provider p) {
    char *openai[] = {"gpt-4o-mini-transcribe", "gpt-4o-transcribe", "whisper-1", NULL};
    char *elevenlabs[] = {"scribe_v2", "scribe_v1", NULL};
    char *groq[] = {"whisper-large-v3-turbo", "whisper-large-v3", NULL};
    populate_models(p, p == PROVIDER_OPENAI       ? openai
                       : p == PROVIDER_ELEVENLABS ? elevenlabs
                                                  : groq);
}
static void models_worker(GTask *task, void *source, void *data, GCancellable *cancel) {
    (void)source;
    ModelJob *j = data;
    GError *error = NULL;
    char **names = fetch_models(j->key, cancel, &error);
    if (names)
        g_task_return_pointer(task, names, (GDestroyNotify)g_strfreev);
    else
        g_task_return_error(task, error);
}
static void models_ready(GObject *obj, GAsyncResult *result, void *user) {
    (void)obj;
    (void)user;
    GTask *task = G_TASK(result);
    ModelJob *j = g_task_get_task_data(task);
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) names = g_task_propagate_pointer(task, &error);
    if (j->generation != app.model_generation ||
        g_strcmp0(j->key, gtk_entry_get_text(GTK_ENTRY(app.key))))
        return;
    g_clear_object(&app.models_cancel);
    gtk_widget_set_sensitive(app.refresh, app.state == IDLE);
    if (error) {
        if (app.state == IDLE)
            set_status(error->message);
        return;
    }
    populate_models(provider_from_key(j->key), names);
    if (app.state == IDLE)
        set_status("모델 목록을 불러왔습니다. 모델을 선택하고 설정을 저장하세요.");
}
static gboolean refresh_models(void *user) {
    (void)user;
    app.model_timer = 0;
    if (app.state != IDLE)
        return G_SOURCE_REMOVE;
    const char *key = gtk_entry_get_text(GTK_ENTRY(app.key));
    if (!provider_from_key(key) || strlen(key) < 8)
        return G_SOURCE_REMOVE;
    if (app.models_cancel) {
        g_cancellable_cancel(app.models_cancel);
        g_clear_object(&app.models_cancel);
    }
    app.models_cancel = g_cancellable_new();
    ModelJob *j = g_new0(ModelJob, 1);
    j->key = g_strdup(key);
    j->generation = ++app.model_generation;
    GTask *task = g_task_new(NULL, app.models_cancel, models_ready, NULL);
    g_task_set_task_data(task, j, model_job_free);
    gtk_widget_set_sensitive(app.refresh, FALSE);
    set_status("모델 목록을 불러오는 중…");
    g_task_run_in_thread(task, models_worker);
    g_object_unref(task);
    return G_SOURCE_REMOVE;
}
static void refresh_clicked(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    if (app.model_timer) {
        g_source_remove(app.model_timer);
        app.model_timer = 0;
    }
    refresh_models(NULL);
}
static void provider_changed(GtkEditable *w, void *user) {
    (void)user;
    ++app.model_generation;
    if (app.models_cancel) {
        g_cancellable_cancel(app.models_cancel);
        g_clear_object(&app.models_cancel);
    }
    if (app.model_timer) {
        g_source_remove(app.model_timer);
        app.model_timer = 0;
    }
    Provider p = provider_from_key(gtk_entry_get_text(GTK_ENTRY(w)));
    gtk_widget_set_sensitive(app.refresh, app.state == IDLE && p != PROVIDER_NONE);
    gtk_widget_set_sensitive(app.model, app.state == IDLE && p != PROVIDER_NONE);
    if (p != app.model_provider) {
        app.model_provider = p;
        default_models(p);
    }
    if (p)
        app.model_timer = g_timeout_add(600, refresh_models, NULL);
}
static void save_settings(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    if (app.state != IDLE) {
        set_status("녹음·전사가 끝난 뒤 설정을 저장하세요");
        return;
    }
    Settings next;
    settings_copy(&next, &app.settings);
#define ENTRY(dest, widget)                                                                        \
    g_free(dest);                                                                                  \
    dest = g_strdup(gtk_entry_get_text(GTK_ENTRY(widget)));                                        \
    g_strstrip(dest)
    ENTRY(next.api_key, app.key);
    g_free(next.language);
    next.language = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.language)));
    Provider p = provider_from_key(next.api_key);
    if (*next.api_key && !p) {
        settings_clear(&next);
        set_status("API 키 형식을 확인하세요 (sk-, sk_, gsk_)");
        return;
    }
    for (int i = 1; i < 4; i++)
        if (app.draft_models[i]) {
            g_free(next.models[i]);
            next.models[i] = g_strdup(app.draft_models[i]);
        }
    if (p) {
        const char *model = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.model));
        g_free(next.models[p]);
        next.models[p] = g_strdup(model ? model : "");
        if (!*next.models[p]) {
            settings_clear(&next);
            set_status("모델을 선택하세요");
            return;
        }
    }
    g_free(next.keyterms);
    g_autofree char *terms = view_text(app.terms);
    g_auto(GStrv) entries = g_strsplit_set(terms, "\n,", -1);
    GString *normalized = g_string_new(NULL);
    guint count = 0;
    for (int i = 0; entries[i] && count < 100; i++) {
        char *term = g_strstrip(entries[i]);
        if (*term) {
            if (count++)
                g_string_append_c(normalized, '\n');
            g_string_append(normalized, term);
        }
    }
    next.keyterms = g_string_free(normalized, FALSE);
    g_free(next.replacements);
    next.replacements = view_text(app.rules);
    if (strchr(next.api_key, '\n') || strchr(next.api_key, '\r')) {
        settings_clear(&next);
        set_status("API 키에는 줄바꿈을 넣을 수 없습니다");
        return;
    }
    next.hold = g_strcmp0(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.hold)), "hold") == 0;
    next.auto_send = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.send));
    next.no_verbatim = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.no_verbatim));
    next.mute_during_recording = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.mute));
    next.sound_volume = lrint(gtk_range_get_value(GTK_RANGE(app.volume)));
    g_free(next.overlay_position);
    next.overlay_position = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.position)));
    g_free(next.shortcut);
    next.shortcut = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.shortcut_choice)));
    gboolean rebind = !app.portal.shortcuts ||
                      g_strcmp0(next.shortcut, app.settings.shortcut) != 0;
    next.limit_minutes = atoi(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.limit)));
    next.retention_hours = atoi(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.retention)));
    g_autoptr(GError) error = NULL;
    if (!settings_save(&next, app.config_dir, &error)) {
        settings_clear(&next);
        set_status(error->message);
        return;
    }
    settings_clear(&app.settings);
    app.settings = next;
    prune_recordings(app.logs_dir, next.retention_hours);
    set_status("설정을 적용했습니다");
    if (rebind)
        bind_shortcuts(NULL, NULL);
}
static void replacement_help(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    GtkWidget *dialog =
        gtk_message_dialog_new(GTK_WINDOW(app.window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                               GTK_BUTTONS_CLOSE, "치환 단어 사용법");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "한 줄에 찾을 말 => 바꿀 말\n예: 지피티 => GPT\n전송해줘 => [enter]\n\n[enter], [tab], "
        "[esc], [backspace], [delete], 화살표, home/end, pageup/pagedown, f1~f12\n[ctrl+k], "
        "[cmd+k], [alt+tab], [ctrl+shift+p]\ncmd는 Linux에서 Ctrl로 동작합니다. 모르는 대괄호 "
        "내용은 글자로 남습니다.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
static void discard_settings(GtkWidget *w, void *user) {
    (void)w;
    (void)user;
    gtk_entry_set_text(GTK_ENTRY(app.key), app.settings.api_key);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.language), app.settings.language);
    for (int i = 1; i < 4; i++) {
        g_free(app.draft_models[i]);
        app.draft_models[i] = g_strdup(app.settings.models[i]);
    }
    default_models(provider_from_key(app.settings.api_key));
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.terms)),
                             app.settings.keyterms, -1);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.rules)),
                             app.settings.replacements, -1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.send), app.settings.auto_send);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.no_verbatim), app.settings.no_verbatim);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mute), app.settings.mute_during_recording);
    gtk_range_set_value(GTK_RANGE(app.volume), app.settings.sound_volume);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.position), app.settings.overlay_position);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.shortcut_choice), app.settings.shortcut);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.hold), app.settings.hold ? "hold" : "toggle");
    g_autofree char *limit = g_strdup_printf("%d", app.settings.limit_minutes),
                    *retention = g_strdup_printf("%d", app.settings.retention_hours);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.limit), limit);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.retention), retention);
    set_status("변경을 취소했습니다");
}
static void create_tray_icons(void) {
    const char *names[] = {"idle", "recording", "transcribing"};
    const guint8 colors[3][3] = {{14, 165, 233}, {232, 52, 52}, {240, 158, 32}};
    g_autofree char *dir = g_build_filename(g_get_user_cache_dir(), "keyscribe", NULL);
    g_mkdir_with_parents(dir, 0700);
    GInputStream *data =
        g_memory_input_stream_new_from_data(keyscribe_menu, sizeof(keyscribe_menu), NULL);
    GdkPixbuf *source = gdk_pixbuf_new_from_stream(data, NULL, NULL);
    g_object_unref(data);
    if (!source)
        return;
    // Use the original menu asset and Windows crop/tints, rather than a new icon.
    GdkPixbuf *cropped = gdk_pixbuf_new_subpixbuf(source, 5, 5, 54, 54);
    GdkPixbuf *icon = gdk_pixbuf_scale_simple(cropped, 64, 64, GDK_INTERP_BILINEAR);
    g_object_unref(cropped);
    g_object_unref(source);
    for (int i = 0; i < 3; i++) {
        app.tray_icons[i] = g_strdup_printf("%s/%s.png", dir, names[i]);
        GdkPixbuf *tinted = gdk_pixbuf_copy(icon);
        guchar *pixels = gdk_pixbuf_get_pixels(tinted);
        int stride = gdk_pixbuf_get_rowstride(tinted), channels = gdk_pixbuf_get_n_channels(tinted);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                for (int c = 0; c < 3; c++)
                    pixels[y * stride + x * channels + c] = colors[i][c];
        gdk_pixbuf_save(tinted, app.tray_icons[i], "png", NULL, NULL);
        g_object_unref(tinted);
    }
    g_object_unref(icon);
}
static gboolean signal_quit(void *user) {
    (void)user;
    g_application_quit(G_APPLICATION(app.application));
    return G_SOURCE_CONTINUE;
}
static GtkWidget *button(GtkWidget *box, const char *label, GCallback cb) {
    GtkWidget *w = gtk_button_new_with_label(label);
    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    g_signal_connect(w, "clicked", cb, NULL);
    return w;
}
static GtkWidget *entry(GtkWidget *grid, int row, const char *label, const char *value) {
    GtkWidget *l = gtk_label_new(label), *w = gtk_entry_new();
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_entry_set_text(GTK_ENTRY(w), value);
    gtk_widget_set_hexpand(w, TRUE);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
    return w;
}
static GtkWidget *text_view(GtkWidget *box, const char *label, const char *text, int height) {
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(label), FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL), *view = gtk_text_view_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(scroll), "input-area");
    gtk_style_context_add_class(gtk_widget_get_style_context(view), "input-area");
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text, -1);
    gtk_widget_set_size_request(scroll, -1, height);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    return view;
}
static GtkWidget *combo(GtkWidget *box, const char *label, const char **ids, const char **labels,
                        int n, int active) {
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(label), FALSE, FALSE, 0);
    GtkWidget *w = gtk_combo_box_text_new();
    for (int i = 0; i < n; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), ids[i], labels[i]);
    g_autofree char *id = g_strdup_printf("%d", active);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(w), id);
    gtk_box_pack_start(GTK_BOX(box), w, FALSE, FALSE, 0);
    return w;
}
static void form_row(GtkWidget *grid, int row, const char *label, GtkWidget *widget) {
    if (gtk_widget_get_parent(widget)) {
        g_object_ref(widget);
        gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(widget)), widget);
    }
    GtkWidget *title = gtk_label_new(label);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_valign(title, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), title, 0, row, 1, 1);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    g_object_unref(widget);
}
static GtkWidget *detach(GtkWidget *widget) {
    g_object_ref(widget);
    gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(widget)), widget);
    return widget;
}
static gboolean prune_tick(void *user) {
    (void)user;
    prune_recordings(app.logs_dir, app.settings.retention_hours);
    return G_SOURCE_CONTINUE;
}
static char *language_name(const char *code) {
    g_autoptr(JsonParser) parser = json_parser_new();
    if (json_parser_load_from_file(parser, "/usr/share/iso-codes/json/iso_639-2.json", NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonNode *entries = JSON_NODE_HOLDS_OBJECT(root)
                                ? json_object_get_member(json_node_get_object(root), "639-2")
                                : NULL;
        if (entries && JSON_NODE_HOLDS_ARRAY(entries)) {
            JsonArray *array = json_node_get_array(entries);
            for (guint i = 0; i < json_array_get_length(array); i++) {
                JsonObject *language = json_array_get_object_element(array, i);
                if (json_object_has_member(language, "alpha_2") &&
                    g_str_equal(json_object_get_string_member(language, "alpha_2"), code))
                    return g_strdup(
                        dgettext("iso_639-2", json_object_get_string_member(language, "name")));
            }
        }
    }
    return g_strdup(code);
}
static void activate(GtkApplication *application, void *user) {
    (void)user;
    if (app.window) {
        show_window(NULL, NULL);
        return;
    }
    app.application = application;
    debug_log_init(app.logs_dir);
    debug_log("app start");
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        css,
        "scrolledwindow.input-area { border: 1px solid alpha(@theme_fg_color, 0.28); "
        "border-radius: 5px; }"
        "textview.input-area, textview.input-area text { background-color: "
        "mix(@theme_base_color, @theme_fg_color, 0.06); color: @theme_text_color; }"
        "textview.input-area:focus { border-color: @theme_selected_bg_color; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    app.window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(app.window), "KeyScribe 설정");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 560, 850);
    g_signal_connect(app.window, "delete-event", G_CALLBACK(hide_window), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(key_press), NULL);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 18);
    gtk_container_add(GTK_CONTAINER(app.window), outer);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<big><b>KeyScribe</b></big>  ·  Ubuntu");
    gtk_box_pack_start(GTK_BOX(outer), title, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(title, TRUE);
    app.status = gtk_label_new("준비됨 — API 키를 설정하고 저장하세요");
    gtk_label_set_line_wrap(GTK_LABEL(app.status), TRUE);
    gtk_box_pack_start(GTK_BOX(outer), app.status, FALSE, FALSE, 0);
    app.level = gtk_level_bar_new_for_interval(0, 1);
    gtk_box_pack_start(GTK_BOX(outer), app.level, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(app.level, TRUE);
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), controls, FALSE, FALSE, 0);
    app.record_button = button(controls, "녹음 시작", G_CALLBACK(toggle));
    button(controls, "취소 (Esc)", G_CALLBACK(cancel_action));
    button(controls, "결과 복사", G_CALLBACK(copy_result));
    GtkWidget *tabs = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(outer), tabs, TRUE, TRUE, 0);
    GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *settings_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(settings_scroll), settings);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(settings_scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(settings), 12);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), settings_scroll, gtk_label_new("설정"));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(settings), grid, FALSE, FALSE, 0);
    app.key = entry(grid, 0, "API 키", app.settings.api_key);
    gtk_entry_set_visibility(GTK_ENTRY(app.key), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(app.key), GTK_INPUT_PURPOSE_PASSWORD);
    app.language = gtk_combo_box_text_new();
    bindtextdomain("iso_639-2", "/usr/share/locale");
    bind_textdomain_codeset("iso_639-2", "UTF-8");
    const char *language_codes =
        "af ar hy az be bs bg ca zh hr cs da nl en et fi fr gl de el he hi hu id it ja kn kk ko lv "
        "lt mk ms mr mi ne no fa pl pt ro ru sr sk sl es sw sv tl ta th tr uk ur vi cy";
    g_auto(GStrv) languages = g_strsplit(language_codes, " ", -1);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.language), "", "자동 감지");
    for (int i = 0; languages[i]; i++) {
        g_autofree char *name = language_name(languages[i]);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.language), languages[i], name);
    }
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.language), app.settings.language)) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.language), app.settings.language,
                                  app.settings.language);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.language), app.settings.language);
    }
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("인식 언어"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.language, 1, 1, 1, 1);
    GtkWidget *links = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(settings), links, FALSE, FALSE, 0);
    const char *link_titles[] = {"ElevenLabs 키 ↗", "OpenAI 키 ↗", "Groq 키 ↗"},
               *link_urls[] = {"https://elevenlabs.io/app/developers/api-keys",
                               "https://platform.openai.com/api-keys",
                               "https://console.groq.com/keys"};
    for (int i = 0; i < 3; i++)
        gtk_box_pack_start(GTK_BOX(links),
                           gtk_link_button_new_with_label(link_urls[i], link_titles[i]), FALSE,
                           FALSE, 0);
    Provider p = provider_from_key(app.settings.api_key);
    GtkWidget *model_label = gtk_label_new("모델"),
              *model_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(model_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), model_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), model_row, 1, 2, 1, 1);
    app.model = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(app.model, TRUE);
    gtk_box_pack_start(GTK_BOX(model_row), app.model, TRUE, TRUE, 0);
    app.refresh = button(model_row, "새로고침", G_CALLBACK(refresh_clicked));
    app.model_changed_handler =
        g_signal_connect(app.model, "changed", G_CALLBACK(selected_model), NULL);
    app.model_provider = p;
    default_models(p);
    g_signal_connect(app.key, "changed", G_CALLBACK(provider_changed), NULL);
    app.shortcut_choice = gtk_combo_box_text_new();
    const char *triggers[] = {"CTRL+ALT+space", "CTRL+SHIFT+space", "LOGO+space", "CTRL+ALT+r",
                             "Hangul", "Alt_R", "Control_R"},
               *trigger_labels[] = {"Ctrl+Alt+Space", "Ctrl+Shift+Space", "Super+Space",
                                    "Ctrl+Alt+R", "한/영 키 (Hangul)", "오른쪽 Alt", "오른쪽 Ctrl"};
    for (guint i = 0; i < G_N_ELEMENTS(triggers); i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.shortcut_choice), triggers[i],
                                  trigger_labels[i]);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.shortcut_choice), app.settings.shortcut)) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.shortcut_choice), app.settings.shortcut,
                                  app.settings.shortcut);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.shortcut_choice), app.settings.shortcut);
    }
    gtk_box_pack_start(GTK_BOX(settings),
                       gtk_label_new("녹음 단축키 (최종 키는 시스템 권한 창에서 변경)"), FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(settings), app.shortcut_choice, FALSE, FALSE, 0);
    button(settings, "키를 눌러 지정", G_CALLBACK(choose_shortcut));
    app.hold = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.hold), "hold", "누르는 동안 녹음");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.hold), "toggle",
                              "한번 누르면 시작, 다시 누르면 종료");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.hold), app.settings.hold ? "hold" : "toggle");
    gtk_box_pack_start(GTK_BOX(settings), gtk_label_new("녹음 방식"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(settings), app.hold, FALSE, FALSE, 0);
    app.send = gtk_check_button_new_with_label("자동 붙여넣기 후 Enter 키 누르기");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.send), app.settings.auto_send);
    gtk_box_pack_start(GTK_BOX(settings), app.send, FALSE, FALSE, 0);
    const char *limits[] = {"10", "20", "30", "60"},
               *limit_labels[] = {"10분", "20분", "30분", "60분"};
    app.limit =
        combo(settings, "최대 녹음 시간", limits, limit_labels, 4, app.settings.limit_minutes);
    const char *hours[] = {"1", "24", "168", "720"},
               *hour_labels[] = {"1시간", "1일", "7일", "30일"};
    app.retention =
        combo(settings, "녹음 보관 기간", hours, hour_labels, 4, app.settings.retention_hours);
    app.position = gtk_combo_box_text_new();
    const char *positions[] = {"hidden", "top_left",    "top_center",    "top_right",
                               "center", "bottom_left", "bottom_center", "bottom_right"},
               *position_labels[] = {"표시 안 함", "상단 왼쪽", "상단 중앙", "상단 오른쪽",
                                     "정중앙",     "하단 왼쪽", "하단 중앙", "하단 오른쪽"};
    for (int i = 0; i < 8; i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.position), positions[i],
                                  position_labels[i]);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.position), app.settings.overlay_position))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.position), "bottom_center");
    gtk_box_pack_start(GTK_BOX(settings), gtk_label_new("녹음 위젯 위치"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(settings), app.position, FALSE, FALSE, 0);
    app.terms =
        text_view(settings, "인식 단어 (한 줄에 하나, 최대 100개)", app.settings.keyterms, 70);
    app.rules = text_view(settings, "치환 단어 (한 줄에 찾을 말 => 바꿀 말)",
                          app.settings.replacements, 60);
    button(settings, "치환 단어 사용법", G_CALLBACK(replacement_help));
    app.no_verbatim = gtk_check_button_new_with_label("군더더기 말 제거 (ElevenLabs)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.no_verbatim), app.settings.no_verbatim);
    gtk_box_pack_start(GTK_BOX(settings), app.no_verbatim, FALSE, FALSE, 0);
    app.mute = gtk_check_button_new_with_label("녹음 중 시스템 소리 음소거");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mute), app.settings.mute_during_recording);
    gtk_box_pack_start(GTK_BOX(settings), app.mute, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(settings),
                       gtk_label_new("녹음 시작 효과음 음량 (0: 끔, 100: 기본, 200: 최대)"), FALSE,
                       FALSE, 0);
    app.volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 200, 1);
    gtk_range_set_value(GTK_RANGE(app.volume), app.settings.sound_volume);
    gtk_scale_set_digits(GTK_SCALE(app.volume), 0);
    gtk_box_pack_start(GTK_BOX(settings), app.volume, FALSE, FALSE, 0);
    button(settings, "설정 저장", G_CALLBACK(save_settings));
    button(settings, "변경 취소", G_CALLBACK(discard_settings));
    GtkWidget *words_scroll = gtk_widget_get_parent(app.terms),
              *rules_scroll = gtk_widget_get_parent(app.rules);
    GtkWidget *compact = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(compact), 16);
    gtk_grid_set_row_spacing(GTK_GRID(compact), 8);
    detach(app.shortcut_choice);
    detach(app.hold);
    detach(app.limit);
    detach(app.retention);
    detach(app.position);
    detach(words_scroll);
    detach(rules_scroll);
    detach(app.no_verbatim);
    detach(app.mute);
    detach(app.send);
    detach(app.volume);
    detach(grid);
    detach(links);
    GList *old = gtk_container_get_children(GTK_CONTAINER(settings));
    for (GList *node = old; node; node = node->next)
        gtk_widget_destroy(node->data);
    g_list_free(old);
    gtk_box_pack_start(GTK_BOX(settings), grid, FALSE, FALSE, 0);
    g_object_unref(grid);
    gtk_box_pack_start(GTK_BOX(settings), links, FALSE, FALSE, 0);
    g_object_unref(links);
    gtk_box_pack_start(GTK_BOX(settings), compact, FALSE, FALSE, 0);
    GtkWidget *shortcut_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(shortcut_row), app.shortcut_choice, TRUE, TRUE, 0);
    g_object_unref(app.shortcut_choice);
    button(shortcut_row, "키 지정", G_CALLBACK(choose_shortcut));
    g_object_ref_sink(shortcut_row);
    form_row(compact, 0, "녹음 단축키", shortcut_row);
    form_row(compact, 1, "녹음 방식", app.hold);
    GtkWidget *time_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(time_row), app.limit, TRUE, TRUE, 0);
    g_object_unref(app.limit);
    gtk_box_pack_start(GTK_BOX(time_row), gtk_label_new("로그·녹음 보존"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_row), app.retention, TRUE, TRUE, 0);
    g_object_unref(app.retention);
    g_object_ref_sink(time_row);
    form_row(compact, 2, "녹음 시간 제한", time_row);
    form_row(compact, 3, "녹음 위젯 위치", app.position);
    form_row(compact, 4, "인식 단어\n(한 줄에 하나)", words_scroll);
    form_row(compact, 5, "치환 단어\n찾을 말 => 바꿀 말", rules_scroll);
    GtkWidget *help_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    button(help_row, "사용법", G_CALLBACK(replacement_help));
    gtk_grid_attach(GTK_GRID(compact), help_row, 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(compact), app.no_verbatim, 1, 6, 1, 1);
    g_object_unref(app.no_verbatim);
    gtk_grid_attach(GTK_GRID(compact), app.mute, 1, 7, 1, 1);
    g_object_unref(app.mute);
    gtk_grid_attach(GTK_GRID(compact), app.send, 1, 8, 1, 1);
    g_object_unref(app.send);
    form_row(compact, 9, "녹음 시작 효과음", app.volume);
    GtkWidget *save_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(save_row, GTK_ALIGN_END);
    button(save_row, "적용", G_CALLBACK(save_settings));
    GtkWidget *close = button(save_row, "닫기", G_CALLBACK(close_settings));
    gtk_widget_set_tooltip_text(close, "적용하지 않은 변경을 버리고 설정 창을 닫습니다.");
    gtk_grid_attach(GTK_GRID(compact), save_row, 1, 10, 1, 1);
    GtkWidget *integration = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(integration), 12);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), integration, gtk_label_new("단축키 / 권한"));
    GtkWidget *help = gtk_label_new(
        "입력할 앱에 커서를 놓고 전역 단축키로 녹음하세요.\n"
        "녹음 키 하나로 시작과 종료를 제어합니다.\n토글 / 누르는 동안 녹음은 설정에서 선택합니다.\n\n자동 입력을 "
        "사용하려면 아래에서 키보드 권한을 허용하세요.\n권한 없이도 결과를 복사해 Ctrl+V로 입력할 "
        "수 있습니다.\n창을 닫아도 트레이에서 계속 실행됩니다.");
    gtk_label_set_line_wrap(GTK_LABEL(help), TRUE);
    gtk_box_pack_start(GTK_BOX(integration), help, FALSE, FALSE, 0);
    button(integration, "자동 붙여넣기 권한 허용", G_CALLBACK(enable_keyboard));
    button(integration, "녹음 폴더 열기", G_CALLBACK(open_logs));
    GtkWidget *result_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(result_box), 12);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), result_box, gtk_label_new("전사 결과"));
    app.result = text_view(result_box, "최근 전사 결과", "", 180);
    GtkWidget *menu = gtk_menu_new();
    app.menu_status = gtk_menu_item_new_with_label("준비됨");
    gtk_widget_set_sensitive(app.menu_status, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), app.menu_status);
    const char *names[] = {"설정 / 결과",    "녹음 시작 / 종료", "취소",
                           "로그·녹음 폴더", "다시 시작",        "종료"};
    GCallback callbacks[] = {G_CALLBACK(show_window),   G_CALLBACK(toggle),
                             G_CALLBACK(cancel_action), G_CALLBACK(open_logs),
                             G_CALLBACK(restart),       G_CALLBACK(quit)};
    for (int i = 0; i < 6; i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(names[i]);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect(item, "activate", callbacks[i], NULL);
    }
    gtk_widget_show_all(menu);
    create_tray_icons();
    app.indicator = g_object_new(APP_INDICATOR_TYPE, "id", "keyscribe", "icon-name",
                                 "audio-input-microphone", "category", "ApplicationStatus", NULL);
    app_indicator_set_status(app.indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(app.indicator, GTK_MENU(menu));
    app.audio_loop = pa_glib_mainloop_new(NULL);
    output_init(&app.output, app.audio_loop);
    overlay_init(&app.overlay);
    g_autoptr(GError) error = NULL;
    if (!portal_init(&app.portal, shortcut, NULL, &error))
        set_status(error->message);
    app.portal.trace = debug_log;
    app.portal.keyboard_closed = keyboard_closed;
    app.portal.keyboard_token_path = g_build_filename(app.config_dir, "keyboard-restore-token", NULL);
    g_file_get_contents(app.portal.keyboard_token_path, &app.portal.keyboard_restore_token,
                        NULL, NULL);
    set_state(IDLE);
    selected_model(GTK_COMBO_BOX(app.model), NULL);
    gtk_widget_show_all(app.window);
    if (provider_from_key(app.settings.api_key) && !app.show_on_start)
        gtk_widget_hide(app.window);
    if (provider_from_key(app.settings.api_key))
        app.model_timer = g_timeout_add(100, refresh_models, NULL);
    g_application_hold(G_APPLICATION(application));
    g_unix_signal_add(SIGTERM, signal_quit, NULL);
    g_unix_signal_add(SIGINT, signal_quit, NULL);
    g_timeout_add_seconds(60, prune_tick, NULL);
    if (app.settings.shortcuts_enabled)
        bind_shortcuts(NULL, NULL);
    if (app.portal.keyboard_restore_token && *app.portal.keyboard_restore_token)
        portal_enable_keyboard(&app.portal, keyboard_done, NULL);
}
int main(int argc, char **argv) {
    if (argc == 2 && g_str_equal(argv[1], "--settings")) {
        app.show_on_start = TRUE;
        argc = 1;
    }
    if (argc == 3 && g_str_equal(argv[1], "--restart-delayed")) {
        pid_t old_pid = (pid_t)g_ascii_strtoll(argv[2], NULL, 10);
        if (old_pid > 0)
            for (int i = 0; i < 50 && kill(old_pid, 0) == 0; i++)
                g_usleep(100000);
        argc = 1;
    }
    if (argc == 2 && g_str_equal(argv[1], "--overlay"))
        return overlay_run();
    if (argc == 2 && g_str_equal(argv[1], "--version")) {
        puts("KeyScribe Linux " KEYSCRIBE_VERSION);
        return 0;
    }
    signal(SIGPIPE, SIG_IGN);
    umask(0077);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    settings_init(&app.settings);
    app.config_dir = g_build_filename(g_get_user_config_dir(), "keyscribe", NULL);
    app.logs_dir = g_build_filename(g_get_user_data_dir(), "keyscribe", "logs", NULL);
    if (g_mkdir_with_parents(app.logs_dir, 0700) < 0) {
        fputs("Cannot create recordings directory\n", stderr);
        return 1;
    }
    g_autoptr(GError) error = NULL;
    if (!settings_load(&app.settings, app.config_dir, &error))
        g_printerr("Cannot load settings: %s\n", error->message);
    prune_recordings(app.logs_dir, app.settings.retention_hours);
    GtkApplication *application =
        gtk_application_new("net.gitools.keyscribe", G_APPLICATION_DEFAULT_FLAGS);
    const GActionEntry actions[] = {
        {.name = "settings", .activate = application_action},
        {.name = "choose-shortcut", .activate = application_action},
        {.name = "enable-keyboard", .activate = application_action},
        {.name = "cancel", .activate = application_action},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(application), actions, G_N_ELEMENTS(actions), NULL);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    app.closing = TRUE;
    if (app.state == RECORDING || app.state == CONNECTING)
        stop_recording(TRUE);
    if (app.cancel)
        g_cancellable_cancel(app.cancel);
    if (app.models_cancel)
        g_cancellable_cancel(app.models_cancel);
    cancel_jobs();
    output_close(&app.output);
    overlay_close(&app.overlay);
    // Worker callbacks are no longer dispatched after shutdown. The process owns their lifetime.
    audio_clear();
    portal_clear(&app.portal);
    if (app.audio_loop)
        pa_glib_mainloop_free(app.audio_loop);
    g_object_unref(application);
    settings_clear(&app.settings);
    g_free(app.config_dir);
    g_free(app.logs_dir);
    g_free(app.wav);
    // libcurl may still be in use by a cancelled worker; process exit releases it.
    return status;
}
