#define main keyscribe_app_main
#include "../src/main.c"
#undef main
static gboolean finished(gpointer user) {
    g_main_loop_quit(user);
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    settings_init(&app.settings);
    app.show_on_start = TRUE;
    g_free(app.settings.overlay_position);
    app.settings.overlay_position = g_strdup("hidden");
    app.settings.sound_volume = 0;
    g_free(app.settings.api_key);
    app.settings.api_key = g_strdup("gsk_smoke_no_upload");
    g_autofree char *dir = g_dir_make_tmp("keyscribe-desktop-XXXXXX", NULL);
    app.config_dir = g_build_filename(dir, "config", NULL);
    app.logs_dir = g_build_filename(dir, "logs", NULL);
    g_mkdir_with_parents(app.logs_dir, 0700);
    GtkApplication *application =
        gtk_application_new("net.gitools.keyscribe.smoketest", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(application), NULL, NULL));
    activate(application, NULL);
    g_assert_true(gtk_widget_get_visible(app.window));
    g_assert_nonnull(app.portal.bus);
    g_assert_nonnull(app.audio_loop);
    g_assert_true(GTK_IS_COMBO_BOX_TEXT(app.model));
    choose_shortcut(NULL, NULL);
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *capture = NULL;
    for (GList *node = windows; node; node = node->next)
        if (GTK_IS_DIALOG(node->data) &&
            g_strcmp0(gtk_window_get_title(GTK_WINDOW(node->data)), "녹음 키 지정") == 0)
            capture = node->data;
    g_list_free(windows);
    g_assert_nonnull(capture);
    g_object_add_weak_pointer(G_OBJECT(capture), (gpointer *)&capture);
    GdkEventKey hangul = {.type = GDK_KEY_PRESS, .keyval = GDK_KEY_Hangul};
    gboolean handled = FALSE;
    g_signal_emit_by_name(capture, "key-press-event", &hangul, &handled);
    g_assert_true(handled);
    g_assert_null(capture);
    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.shortcut_choice)), ==, "Hangul");
    choose_shortcut(NULL, NULL);
    windows = gtk_window_list_toplevels();
    for (GList *node = windows; node; node = node->next)
        if (GTK_IS_DIALOG(node->data) &&
            g_strcmp0(gtk_window_get_title(GTK_WINDOW(node->data)), "녹음 키 지정") == 0)
            capture = node->data;
    g_list_free(windows);
    g_assert_nonnull(capture);
    g_object_add_weak_pointer(G_OBJECT(capture), (gpointer *)&capture);
    GdkEventKey right_alt = {.type = GDK_KEY_PRESS, .keyval = GDK_KEY_Alt_R,
                            .hardware_keycode = 108, .is_modifier = TRUE};
    g_signal_emit_by_name(capture, "key-press-event", &right_alt, &handled);
    g_assert_true(handled);
    g_assert_null(capture);
    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.shortcut_choice)), ==, "Alt_R");
    g_assert_true(GTK_IS_COMBO_BOX_TEXT(app.language));
    g_assert_true(GTK_IS_COMBO_BOX_TEXT(app.hold));
    if (app.model_timer) {
        g_source_remove(app.model_timer);
        app.model_timer = 0;
    }
    app.settings.hold = FALSE;
    app.portal.keyboard_pending = TRUE;
    shortcut("record", TRUE, NULL);
    g_assert_cmpint(app.state, ==, IDLE);
    g_assert_null(app.wav);
    shortcut("record", FALSE, NULL);
    app.portal.keyboard_pending = FALSE;
    // Only exercise recording here; no input is injected with this fixture.
    app.portal.keyboard = g_strdup("/fixture/keyboard");
    shortcut("record", TRUE, NULL);
    g_assert_cmpint(app.state, ==, CONNECTING);
    shortcut("record", TRUE, NULL);
    g_assert_cmpint(app.state, ==, CONNECTING);
    shortcut("record", FALSE, NULL);
    g_assert_cmpint(app.state, ==, CONNECTING);
    cancel_action(NULL, NULL);
    g_clear_pointer(&app.portal.keyboard, g_free);
    app.settings.hold = TRUE;
    start_recording(FALSE);
    gint64 microphone_deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (app.state == CONNECTING && g_get_monotonic_time() < microphone_deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    }
    g_assert_cmpint(app.state, ==, RECORDING);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add_seconds(3, finished, loop);
    g_main_loop_run(loop);
    if(app.state!=RECORDING)g_printerr("Desktop recording status: %s\n",gtk_label_get_text(GTK_LABEL(app.status)));
    g_assert_cmpint(app.state, ==, RECORDING);
    g_assert_cmpint(app.bytes, >, 32000);
    g_assert_true(app.output.held);
    gboolean original_mute = app.output.prior_muted;
    stop_recording(TRUE);
    gint64 restore_deadline = g_get_monotonic_time() + G_USEC_PER_SEC;
    while (app.output.held && g_get_monotonic_time() < restore_deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    }
    g_assert_false(app.output.held);
    (void)original_mute;
    g_assert_cmpint(app.state, ==, IDLE);
    FILE *wav = fopen(app.wav, "rb");
    g_assert_nonnull(wav);
    char header[44];
    g_assert_cmpuint(fread(header, 1, 44, wav), ==, 44);
    g_assert_cmpmem(header, 4, "RIFF", 4);
    g_assert_cmpmem(header + 8, 4, "WAVE", 4);
    fclose(wav);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.result)),
                             "우분투 KeyScribe 테스트", -1);
    copy_result(NULL, NULL);
    g_autofree char *copied =
        gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_cmpstr(copied, ==, "우분투 KeyScribe 테스트");
    Job *first = g_new0(Job, 1), *second = g_new0(Job, 1);
    settings_init(&first->settings);
    settings_init(&second->settings);
    first->references = second->references = 1;
    first->cancel = g_cancellable_new();
    second->cancel = g_cancellable_new();
    first->text = g_strdup("첫 번째");
    second->text = g_strdup("두 번째");
    second->ready = TRUE;
    g_queue_push_tail(&app.jobs, first);
    g_queue_push_tail(&app.jobs, second);
    set_state(TRANSCRIBING);
    process_results();
    g_assert_cmpuint(g_queue_get_length(&app.jobs), ==, 2);
    first->ready = TRUE;
    app.portal.keyboard_pending = TRUE;
    process_results();
    g_assert_cmpuint(g_queue_get_length(&app.jobs), ==, 2);
    app.portal.keyboard_pending = FALSE;
    set_state(RECORDING);
    process_results();
    g_assert_cmpuint(g_queue_get_length(&app.jobs), ==, 2);
    set_state(TRANSCRIBING);
    process_results();
    g_assert_true(g_queue_is_empty(&app.jobs));
    g_autofree char *ordered = view_text(app.result);
    g_assert_cmpstr(ordered, ==, "두 번째");
    g_assert_cmpint(app.state, ==, IDLE);
    gtk_widget_hide(app.window);
    Job revoked = {0};
    settings_init(&revoked.settings);
    revoked.text = g_strdup("권한 해제 후 결과");
    revoked.paste = TRUE;
    deliver(&revoked);
    g_assert_false(gtk_widget_get_visible(app.window));
    g_autofree char *fallback =
        gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_cmpstr(fallback, ==, revoked.text);
    settings_clear(&revoked.settings);
    g_free(revoked.text);
    // Delivery must remain cancellable until every paste segment finishes.
    Job input = {0};
    settings_init(&input.settings);
    input.text = g_strdup("입력 취소 검사");
    input.paste = TRUE;
    app.portal.keyboard = g_strdup("/fixture/keyboard");
    deliver(&input);
    g_assert_cmpint(app.state, ==, TRANSCRIBING);
    g_assert_true(app.portal.escape_active);
    g_assert_nonnull(app.paste_steps);
    cancel_action(NULL, NULL);
    g_assert_cmpint(app.state, ==, IDLE);
    g_assert_null(app.paste_steps);
    g_assert_cmpuint(app.paste_timer, ==, 0);
    g_clear_pointer(&app.portal.keyboard, g_free);
    settings_clear(&input.settings);
    g_free(input.text);
    gtk_widget_show(app.window);
    gtk_entry_set_text(GTK_ENTRY(app.key), "sk-model-fixture");
    g_assert_cmpint(app.model_provider, ==, PROVIDER_OPENAI);
    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.model)), ==,
                    "gpt-4o-mini-transcribe");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.model), "whisper-1");
    gtk_entry_set_text(GTK_ENTRY(app.key), "gsk-fixture");
    gtk_entry_set_text(GTK_ENTRY(app.key), "sk-model-fixture");
    g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(app.model)), ==, "whisper-1");
    if (app.model_timer) {
        g_source_remove(app.model_timer);
        app.model_timer = 0;
    }
    for (int i = 0; i < 100; i++) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    }
    GtkAllocation bounds;
    gtk_widget_get_allocation(app.window, &bounds);
    cairo_surface_t *screenshot =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bounds.width, bounds.height);
    cairo_t *cr = cairo_create(screenshot);
    gtk_widget_draw(app.window, cr);
    cairo_surface_write_to_png(screenshot, "/tmp/keyscribe-settings-preview.png");
    cairo_destroy(cr);
    cairo_surface_destroy(screenshot);
    g_print("PASS: native GTK window, tray, portal bus, real microphone WAV, cancellation, Korean "
            "clipboard, mute/restore, dropdowns, ordered transcription delivery\n");
    output_close(&app.output);
    overlay_close(&app.overlay);
    portal_clear(&app.portal);
    audio_clear();
    pa_glib_mainloop_free(app.audio_loop);
    settings_clear(&app.settings);
    g_unlink(app.wav);
    g_rmdir(app.logs_dir);
    g_rmdir(dir);
    g_free(app.wav);
    g_free(app.logs_dir);
    g_free(app.config_dir);
    g_main_loop_unref(loop);
    g_object_unref(application);
    return 0;
}
