#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <string.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>
static void providers(void) {
    g_assert_cmpint(provider_from_key("gsk_test"), ==, PROVIDER_GROQ);
    g_assert_cmpint(provider_from_key("sk-test"), ==, PROVIDER_OPENAI);
    g_assert_cmpint(provider_from_key("sk_test"), ==, PROVIDER_ELEVENLABS);
    g_assert_cmpint(provider_from_key("bad"), ==, PROVIDER_NONE);
}
static void text(void) {
    g_autofree char *s = clean_text("  안녕 [noise] \n 세계  [unfinished");
    g_assert_cmpstr(s, ==, "안녕 세계 [unfinished");
    g_autofree char *r = apply_replacements(
        "안녕 키스크라이브", "키스크라이브 => KeyScribe\n안녕 -> 반가워\n => invalid");
    g_assert_cmpstr(r, ==, "반가워 KeyScribe");
}
static void settings_cleanup(const char *dir) {
    const char *names[] = {"config.toml", "user_config.json", "config.ini", "settings.ini"};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        g_autofree char *path = g_build_filename(dir, names[i], NULL);
        g_unlink(path);
    }
    g_assert_cmpint(g_rmdir(dir), ==, 0);
}
static char *settings_read(const char *dir, const char *name) {
    g_autofree char *path = g_build_filename(dir, name, NULL);
    char *text = NULL;
    g_autoptr(GError) error = NULL;
    g_assert_true(g_file_get_contents(path, &text, NULL, &error));
    g_assert_no_error(error);
    return text;
}
static void settings_write(const char *dir, const char *name, const char *text) {
    g_autofree char *path = g_build_filename(dir, name, NULL);
    g_autoptr(GError) error = NULL;
    g_assert_true(g_file_set_contents(path, text, -1, &error));
    g_assert_no_error(error);
}
static void settings_private_file(const char *dir, const char *name) {
    g_autofree char *path = g_build_filename(dir, name, NULL);
    struct stat st;
    g_assert_cmpint(stat(path, &st), ==, 0);
    g_assert_cmpint(st.st_mode & 0777, ==, 0600);
}
static void settings(void) {
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = g_dir_make_tmp("keyscribe-settings-XXXXXX", &error);
    g_assert_no_error(error);
    settings_write(dir, "user_config.json",
                   "{\"api_key\":\"gsk_old_dummy\",\"metadata\":{\"keep\":true},\"revision\":7}");
    Settings a, b;
    settings_init(&a);
    settings_init(&b);
    g_free(a.api_key);
    a.api_key = g_strdup("gsk_dummy");
    g_free(a.keyterms);
    a.keyterms = g_strdup("A # B\nquoted \"name\"\n한글 🚀\nACME, Inc.");
    g_free(a.replacements);
    a.replacements = g_strdup("안녕 => hello\n하나 -> 둘\nquote => \"x\"\\path\nA # B => C=D");
    a.hold = FALSE;
    a.auto_send = FALSE;
    a.limit_minutes = 60;
    a.retention_hours = 1;
    a.no_verbatim = FALSE;
    a.mute_during_recording = FALSE;
    a.shortcuts_enabled = TRUE;
    a.sound_volume = 175;
    g_free(a.overlay_position);
    a.overlay_position = g_strdup("top_right");
    g_assert_true(settings_save(&a, dir, &error));
    g_assert_no_error(error);
    g_assert_true(settings_load(&b, dir, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(a.api_key, ==, b.api_key);
    g_assert_cmpstr(a.keyterms, ==, b.keyterms);
    g_assert_cmpstr(a.replacements, ==, b.replacements);
    g_assert_false(b.hold);
    g_assert_false(b.auto_send);
    g_assert_cmpint(b.limit_minutes, ==, 60);
    g_assert_cmpint(b.retention_hours, ==, 1);
    g_assert_false(b.no_verbatim);
    g_assert_false(b.mute_during_recording);
    g_assert_true(b.shortcuts_enabled);
    g_assert_cmpint(b.sound_volume, ==, 175);
    g_assert_cmpstr(b.overlay_position, ==, "top_right");
    g_autofree char *config = settings_read(dir, "config.toml");
    g_assert_null(strstr(config, "gsk_dummy"));
    g_assert_null(strstr(config, "api_key ="));
    g_assert_nonnull(strstr(config, "recording_control"));
    g_assert_nonnull(strstr(config, "recording_time_limit_minutes"));
    g_assert_nonnull(strstr(config, "recording_start_sound_volume"));
    g_assert_nonnull(strstr(config, "log_retention_hours"));
    g_assert_nonnull(strstr(config, "#"));
    g_autofree char *credentials = settings_read(dir, "user_config.json");
    g_autoptr(JsonParser) parser = json_parser_new();
    g_assert_true(json_parser_load_from_data(parser, credentials, -1, &error));
    g_assert_no_error(error);
    JsonObject *user = json_node_get_object(json_parser_get_root(parser));
    g_assert_cmpstr(json_object_get_string_member(user, "api_key"), ==, "gsk_dummy");
    g_assert_true(json_object_get_boolean_member(json_object_get_object_member(user, "metadata"), "keep"));
    g_assert_cmpint(json_object_get_int_member(user, "revision"), ==, 7);
    settings_private_file(dir, "config.toml");
    settings_private_file(dir, "user_config.json");
    settings_clear(&a);
    settings_clear(&b);
    settings_cleanup(dir);
}
static void initial_settings(void) {
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = g_dir_make_tmp("keyscribe-initial-settings-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *path = settings_path(dir);
    g_assert_true(g_str_has_suffix(path, "/config.toml"));
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    g_autofree char *original_key = g_strdup(g_getenv("ELEVENLABS_API_KEY"));
    g_setenv("ELEVENLABS_API_KEY", "sk_private_dummy", TRUE);
    Settings value;
    settings_init(&value);
    g_assert_true(settings_load(&value, dir, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(value.api_key, ==, "sk_private_dummy");
    g_autofree char *config = settings_read(dir, "config.toml");
    g_assert_null(strstr(config, "sk_private_dummy"));
    g_assert_nonnull(strstr(config, "recording_start_sound_volume"));
    g_assert_nonnull(strstr(config, "#"));
    g_autofree char *user_path = g_build_filename(dir, "user_config.json", NULL);
    if (g_file_test(user_path, G_FILE_TEST_EXISTS)) {
        g_autofree char *user = settings_read(dir, "user_config.json");
        g_assert_null(strstr(user, "sk_private_dummy"));
        settings_private_file(dir, "user_config.json");
    }
    settings_private_file(dir, "config.toml");
    if (original_key)
        g_setenv("ELEVENLABS_API_KEY", original_key, TRUE);
    else
        g_unsetenv("ELEVENLABS_API_KEY");
    settings_clear(&value);
    settings_cleanup(dir);
}
static void legacy_settings(void) {
    const char *sources[] = {"settings.ini", "config.ini"};
    const char *legacy_text =
        "# Leave legacy preferences intact\n[settings]\napi_key=gsk_dummy\nlanguage=ja\n"
        "openai_model=custom-openai\nelevenlabs_model=custom-eleven\ngroq_model=custom-groq\n"
        "hold=false\nauto_send=false\nno_verbatim=false\nmute_during_recording=false\n"
        "sound_volume=175\noverlay_position=top_right\nshortcut=CTRL+ALT+r\n"
        "shortcuts_enabled=true\nlimit_minutes=60\nretention_hours=24\n"
        "keyterms=KeyScribe, VideoStew\\n한글\nreplacements=one => two\\nthree => four\n";
    for (guint i = 0; i < G_N_ELEMENTS(sources); i++) {
        g_autoptr(GError) error = NULL;
        g_autofree char *dir = g_dir_make_tmp("keyscribe-legacy-settings-XXXXXX", &error);
        g_assert_no_error(error);
        settings_write(dir, sources[i], legacy_text);
        if (g_str_equal(sources[i], "config.ini"))
            settings_write(dir, "settings.ini", "[settings]\nlanguage=en\napi_key=gsk_stale_dummy\n");
        g_autofree char *legacy = g_build_filename(dir, sources[i], NULL);
        g_autofree char *current = g_build_filename(dir, "config.toml", NULL);
        g_autofree char *before = settings_path(dir);
        g_assert_cmpstr(before, ==, legacy);
        g_assert_false(g_file_test(current, G_FILE_TEST_EXISTS));
        Settings value;
        settings_init(&value);
        g_assert_true(settings_load(&value, dir, &error));
        g_assert_no_error(error);
        g_assert_cmpstr(value.api_key, ==, "gsk_dummy");
        g_assert_cmpstr(value.language, ==, "ja");
        g_assert_cmpstr(value.models[1], ==, "custom-openai");
        g_assert_cmpstr(value.models[2], ==, "custom-eleven");
        g_assert_cmpstr(value.models[3], ==, "custom-groq");
        g_assert_false(value.hold);
        g_assert_false(value.auto_send);
        g_assert_false(value.no_verbatim);
        g_assert_false(value.mute_during_recording);
        g_assert_true(value.shortcuts_enabled);
        g_assert_cmpint(value.sound_volume, ==, 175);
        g_assert_cmpstr(value.overlay_position, ==, "top_right");
        g_assert_cmpstr(value.shortcut, ==, "CTRL+ALT+r");
        g_assert_cmpint(value.limit_minutes, ==, 60);
        g_assert_cmpint(value.retention_hours, ==, 24);
        g_assert_cmpstr(value.replacements, ==, "one => two\nthree => four");
        g_assert_cmpstr(value.keyterms, ==, "KeyScribe\nVideoStew\n한글");
        g_autofree char *migrated_terms = g_strdup(value.keyterms);
        g_autofree char *after = settings_path(dir);
        g_assert_cmpstr(after, ==, current);
        g_autofree char *legacy_unchanged = settings_read(dir, sources[i]);
        g_assert_cmpstr(legacy_unchanged, ==, legacy_text);
        g_autofree char *saved = settings_read(dir, "config.toml");
        g_assert_null(strstr(saved, "gsk_dummy"));
        settings_private_file(dir, "config.toml");
        settings_private_file(dir, "user_config.json");
        settings_write(dir, sources[i], "[settings]\nlanguage=en\napi_key=gsk_other_dummy\n");
        settings_clear(&value);
        settings_init(&value);
        g_assert_true(settings_load(&value, dir, &error));
        g_assert_no_error(error);
        g_assert_cmpstr(value.language, ==, "ja");
        g_assert_cmpstr(value.api_key, ==, "gsk_dummy");
        g_assert_cmpstr(value.keyterms, ==, migrated_terms);
        g_assert_cmpstr(value.replacements, ==, "one => two\nthree => four");
        g_assert_false(value.hold);
        g_assert_false(value.auto_send);
        settings_clear(&value);
        settings_cleanup(dir);
    }
}
static void migration_credentials(void) {
    const char *users[] = {
        "{\"api_key\":\"gsk_json_dummy\",\"metadata\":{\"keep\":true}}",
        "{\"metadata\":{\"keep\":true}}",
    };
    const char *keys[] = {"gsk_json_dummy", "gsk_legacy_dummy"};
    for (guint i = 0; i < G_N_ELEMENTS(users); i++) {
        g_autofree char *dir = g_dir_make_tmp("keyscribe-migration-credentials-XXXXXX", NULL);
        const char *legacy = "[settings]\nlanguage=ja\napi_key=gsk_legacy_dummy\n";
        settings_write(dir, "config.ini", legacy);
        settings_write(dir, "user_config.json", users[i]);
        Settings value;
        settings_init(&value);
        g_autoptr(GError) error = NULL;
        g_assert_true(settings_load(&value, dir, &error));
        g_assert_no_error(error);
        g_assert_cmpstr(value.api_key, ==, keys[i]);
        g_autofree char *migrated = settings_read(dir, "user_config.json");
        g_autoptr(JsonParser) parser = json_parser_new();
        g_assert_true(json_parser_load_from_data(parser, migrated, -1, &error));
        g_assert_no_error(error);
        JsonObject *user = json_node_get_object(json_parser_get_root(parser));
        g_assert_cmpstr(json_object_get_string_member(user, "api_key"), ==, keys[i]);
        g_assert_true(json_object_get_boolean_member(json_object_get_object_member(user, "metadata"), "keep"));
        g_autofree char *unchanged = settings_read(dir, "config.ini");
        g_assert_cmpstr(unchanged, ==, legacy);
        settings_private_file(dir, "user_config.json");
        settings_clear(&value);
        settings_cleanup(dir);
    }
}
static void toml_boundaries(void) {
    g_autofree char *dir = g_dir_make_tmp("keyscribe-toml-boundaries-XXXXXX", NULL);
    g_autoptr(GString) config = g_string_new(
        "recording_start_sound_volume = 200\nrecording_time_limit_minutes = 10\n"
        "log_retention_hours = 720\nkeyterms = [");
    for (int i = 0; i < 100; i++)
        g_string_append_printf(config, "\"term-%d\",", i);
    g_string_append(config, "]\n");
    settings_write(dir, "config.toml", config->str);
    Settings value;
    settings_init(&value);
    g_autoptr(GError) error = NULL;
    g_assert_true(settings_load(&value, dir, &error));
    g_assert_no_error(error);
    g_assert_cmpint(value.sound_volume, ==, 200);
    g_assert_cmpint(value.limit_minutes, ==, 10);
    g_assert_cmpint(value.retention_hours, ==, 720);
    g_auto(GStrv) terms = g_strsplit(value.keyterms, "\n", -1);
    g_assert_cmpuint(g_strv_length(terms), ==, 100);
    g_assert_cmpstr(terms[99], ==, "term-99");
    settings_clear(&value);
    settings_cleanup(dir);
}
static void toml_syntax(void) {
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = g_dir_make_tmp("keyscribe-toml-syntax-XXXXXX", &error);
    g_assert_no_error(error);
    settings_write(dir, "config.toml",
        "# TOML edited by an agent\nlanguage = 'ja' # inline comment\n"
        "future_setting = { version = 2 }\n"
        "recording_control = \"toggle\"\nauto_send = false\n"
        "recording_time_limit_minutes = 60\nlog_retention_hours = 24\n"
        "recording_start_sound_volume = 0\noverlay_position = \"hidden\"\n"
        "keyterms = [\n  \"A # B\", # hash inside string\n"
        "  \"quoted \\\"name\\\"\",\n  \"\\uD55C\\uAE00\\U0001F680\",\n]\n"
        "replacements = [\n  \"A # B => C=D\",\n"
        "  'path => C:\\Users\\test',\n  \"tab => \\t\",\n]\n");
    Settings value;
    settings_init(&value);
    g_assert_true(settings_load(&value, dir, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(value.language, ==, "ja");
    g_assert_false(value.hold);
    g_assert_false(value.auto_send);
    g_assert_cmpint(value.limit_minutes, ==, 60);
    g_assert_cmpint(value.retention_hours, ==, 24);
    g_assert_cmpint(value.sound_volume, ==, 0);
    g_assert_cmpstr(value.overlay_position, ==, "hidden");
    g_assert_cmpstr(value.keyterms, ==, "A # B\nquoted \"name\"\n한글🚀");
    g_assert_cmpstr(value.replacements, ==, "A # B => C=D\npath => C:\\Users\\test\ntab => \t");
    g_assert_true(settings_save(&value, dir, &error));
    g_assert_no_error(error);
    Settings roundtrip;
    settings_init(&roundtrip);
    g_assert_true(settings_load(&roundtrip, dir, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(roundtrip.keyterms, ==, value.keyterms);
    g_assert_cmpstr(roundtrip.replacements, ==, value.replacements);
    settings_clear(&roundtrip);
    settings_clear(&value);
    settings_cleanup(dir);
}
static void invalid_toml(void) {
    g_autoptr(GString) too_many_terms = g_string_new("keyterms = [");
    for (int i = 0; i < 101; i++)
        g_string_append(too_many_terms, "\"term\",");
    g_string_append(too_many_terms, "]\n");
    const char *invalid[] = {
        "language = \"unterminated\n",
        "language = \"ja\"\nlanguage = \"en\"\n",
        "auto_send = \"false\"\n",
        "language = 12\n",
        "recording_control = \"sometimes\"\n",
        "recording_time_limit_minutes = 45\n",
        "recording_time_limit_minutes = \"60\"\n",
        "log_retention_hours = 5\n",
        "recording_start_sound_volume = 201\n",
        "recording_start_sound_volume = -1\n",
        "overlay_position = \"somewhere\"\n",
        "keyterms = [\"KeyScribe\", 42]\n",
        "replacements = \"one => two\"\n",
        "shortcuts_enabled = \"true\"\n",
        "no_verbatim = 1\n",
        "keyterms = [\"line\\nterm\"]\n",
        "replacements = [\"line\\rrule\"]\n",
        "language = \"\\u0000\"\n",
        too_many_terms->str,
    };
    for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_autofree char *dir = g_dir_make_tmp("keyscribe-invalid-toml-XXXXXX", NULL);
        settings_write(dir, "config.toml", invalid[i]);
        settings_write(dir, "config.ini", "[settings]\nlanguage=ja\napi_key=gsk_legacy_dummy\n");
        const char *credentials = "{\"api_key\":\"gsk_preserve_dummy\",\"keep\":true}";
        settings_write(dir, "user_config.json", credentials);
        Settings value;
        settings_init(&value);
        g_autoptr(GError) error = NULL;
        g_test_message("Invalid TOML fixture %u", i);
        g_assert_false(settings_load(&value, dir, &error));
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_autofree char *unchanged = settings_read(dir, "config.toml");
        g_assert_cmpstr(unchanged, ==, invalid[i]);
        g_autofree char *user_unchanged = settings_read(dir, "user_config.json");
        g_assert_cmpstr(user_unchanged, ==, credentials);
        settings_clear(&value);
        settings_cleanup(dir);
    }
}
static void invalid_legacy(void) {
    g_autofree char *dir = g_dir_make_tmp("keyscribe-invalid-legacy-XXXXXX", NULL);
    const char *legacy = "[settings\nlanguage=ja\napi_key=gsk_dummy\n";
    settings_write(dir, "config.ini", legacy);
    Settings value;
    settings_init(&value);
    g_autoptr(GError) error = NULL;
    g_assert_false(settings_load(&value, dir, &error));
    g_assert_nonnull(error);
    g_autofree char *current = g_build_filename(dir, "config.toml", NULL);
    g_autofree char *user = g_build_filename(dir, "user_config.json", NULL);
    g_assert_false(g_file_test(current, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(user, G_FILE_TEST_EXISTS));
    g_autofree char *unchanged = settings_read(dir, "config.ini");
    g_assert_cmpstr(unchanged, ==, legacy);
    settings_clear(&value);
    settings_cleanup(dir);
}
static void invalid_credentials(void) {
    const char *invalid[] = {"{broken json", "[]", "{\"api_key\":42}"};
    const char *config = "language = \"ja\"\nauto_send = false\n";
    for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_autofree char *dir = g_dir_make_tmp("keyscribe-invalid-user-XXXXXX", NULL);
        settings_write(dir, "config.toml", config);
        settings_write(dir, "user_config.json", invalid[i]);
        Settings value;
        settings_init(&value);
        g_autoptr(GError) error = NULL;
        g_assert_false(settings_load(&value, dir, &error));
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_clear_error(&error);
        g_assert_false(settings_save(&value, dir, &error));
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_autofree char *config_unchanged = settings_read(dir, "config.toml");
        g_autofree char *user_unchanged = settings_read(dir, "user_config.json");
        g_assert_cmpstr(config_unchanged, ==, config);
        g_assert_cmpstr(user_unchanged, ==, invalid[i]);
        settings_clear(&value);
        settings_cleanup(dir);
    }
    // Failed credential parsing must not partially migrate a legacy configuration.
    g_autofree char *dir = g_dir_make_tmp("keyscribe-invalid-migration-XXXXXX", NULL);
    const char *legacy = "[settings]\nlanguage=ja\napi_key=gsk_legacy_dummy\n";
    settings_write(dir, "settings.ini", legacy);
    settings_write(dir, "user_config.json", invalid[0]);
    Settings value;
    settings_init(&value);
    g_autoptr(GError) error = NULL;
    g_assert_false(settings_load(&value, dir, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_autofree char *current = g_build_filename(dir, "config.toml", NULL);
    g_assert_false(g_file_test(current, G_FILE_TEST_EXISTS));
    g_autofree char *legacy_unchanged = settings_read(dir, "settings.ini");
    g_autofree char *user_unchanged = settings_read(dir, "user_config.json");
    g_assert_cmpstr(legacy_unchanged, ==, legacy);
    g_assert_cmpstr(user_unchanged, ==, invalid[0]);
    settings_clear(&value);
    settings_cleanup(dir);
}
static void wav(void) {
    FILE *f = tmpfile();
    g_assert_nonnull(f);
    g_assert_true(wav_header(f, 32000));
    rewind(f);
    unsigned char h[44];
    g_assert_cmpint(fread(h, 1, 44, f), ==, 44);
    g_assert_cmpmem(h, 4, "RIFF", 4);
    g_assert_cmpmem(h + 8, 8, "WAVEfmt ", 8);
    g_assert_cmpint(h[24] | h[25] << 8, ==, 16000);
    g_assert_cmpint(h[40] | h[41] << 8, ==, 32000);
    fclose(f);
}
static void retention(void) {
    g_autofree char *dir = g_dir_make_tmp("keyscribe-retention-XXXXXX", NULL);
    g_autofree char *old = g_build_filename(dir, "recording-old.wav", NULL),
                    *recent = g_build_filename(dir, "recording-new.wav", NULL),
                    *other = g_build_filename(dir, "unrelated.wav", NULL);
    g_file_set_contents(old, "old", -1, NULL);
    g_file_set_contents(recent, "new", -1, NULL);
    g_file_set_contents(other, "keep", -1, NULL);
    struct utimbuf times = {.actime = 1, .modtime = 1};
    g_utime(old, &times);
    g_utime(other, &times);
    prune_recordings(dir, 1);
    g_assert_false(g_file_test(old, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(recent, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(other, G_FILE_TEST_EXISTS));
    g_unlink(recent);
    g_unlink(other);
    g_rmdir(dir);
}
static void cancellation(void) {
    g_autofree char *path = NULL;
    int fd = g_file_open_tmp("keyscribe-cancel-XXXXXX.wav", &path, NULL);
    FILE *f = fdopen(fd, "wb");
    g_assert_true(wav_header(f, 0));
    fclose(f);
    Settings s;
    settings_init(&s);
    g_free(s.api_key);
    s.api_key = g_strdup("gsk_dummy");
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_autoptr(GError) e = NULL;
    g_autofree char *result = transcribe(path, &s, cancel, &e);
    g_assert_null(result);
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    settings_clear(&s);
    g_unlink(path);
}
static void key_segments(void) {
    GPtrArray *parts = paste_segments("첫째[enter]둘째[cmd+shift+k][0][unknown]");
    g_assert_cmpuint(parts->len, ==, 5);
    PasteSegment *one = g_ptr_array_index(parts, 0), *enter = g_ptr_array_index(parts, 1),
                 *chord = g_ptr_array_index(parts, 3), *literal = g_ptr_array_index(parts, 4);
    g_assert_cmpstr(one->text, ==, "첫째");
    g_assert_cmpuint(enter->n_keys, ==, 1);
    g_assert_cmpuint(enter->keys[0], ==, 0xff0d);
    g_assert_cmpuint(chord->n_keys, ==, 3);
    g_assert_cmpuint(chord->keys[0], ==, 0xffe3);
    g_assert_cmpuint(chord->keys[1], ==, 0xffe1);
    g_assert_cmpuint(chord->keys[2], ==, 'k');
    g_assert_cmpstr(literal->text, ==, "[0][unknown]");
    g_ptr_array_unref(parts);
    const char *tokens[] = {"[ctrl+k]", "[Ctrl + K]", "[CTRL-K]", "[ ctrl + K ]", "[cmd+k]"};
    for (guint i = 0; i < G_N_ELEMENTS(tokens); i++) {
        parts = paste_segments(tokens[i]);
        g_assert_cmpuint(parts->len, ==, 1);
        PasteSegment *key = g_ptr_array_index(parts, 0);
        g_assert_cmpuint(key->n_keys, ==, 2);
        g_assert_cmpuint(key->keys[0], ==, 0xffe3);
        g_assert_cmpuint(key->keys[1], ==, 'k');
        g_ptr_array_unref(parts);
    }
}
int main(int argc, char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/linux/providers", providers);
    g_test_add_func("/linux/text", text);
    g_test_add_func("/linux/replacement-key-commands", key_segments);
    g_test_add_func("/linux/settings-private-roundtrip", settings);
    g_test_add_func("/linux/settings-legacy-migration", legacy_settings);
    g_test_add_func("/linux/settings-initial-documented-config", initial_settings);
    g_test_add_func("/linux/settings-toml-syntax", toml_syntax);
    g_test_add_func("/linux/settings-migration-credentials-metadata", migration_credentials);
    g_test_add_func("/linux/settings-toml-boundaries", toml_boundaries);
    g_test_add_func("/linux/settings-invalid-toml", invalid_toml);
    g_test_add_func("/linux/settings-invalid-legacy-no-data-loss", invalid_legacy);
    g_test_add_func("/linux/settings-invalid-credentials-no-data-loss", invalid_credentials);
    g_test_add_func("/linux/wav-format", wav);
    g_test_add_func("/linux/retention", retention);
    g_test_add_func("/linux/cancel-upload", cancellation);
    int result = g_test_run();
    curl_global_cleanup();
    return result;
}
