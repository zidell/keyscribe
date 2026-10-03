#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include "../vendor/tomlc17/tomlc17.h"
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <string.h>

#define INVALID(...) g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, __VA_ARGS__)

void settings_init(Settings *s) {
    *s = (Settings){.api_key = g_strdup(""),
                    .language = g_strdup("ko"),
                    .models = {g_strdup(""), g_strdup("gpt-4o-mini-transcribe"),
                               g_strdup("scribe_v2"), g_strdup("whisper-large-v3-turbo")},
                    .keyterms = g_strdup(""),
                    .replacements = g_strdup(""),
                    .auto_send = TRUE,
                    .no_verbatim = TRUE,
                    .mute_during_recording = TRUE,
                    .sound_volume = 100,
                    .overlay_position = g_strdup("bottom_center"),
                    .shortcut = g_strdup("CTRL+ALT+space"),
                    .limit_minutes = 30,
                    .retention_hours = 168};
}
void settings_clear(Settings *s) {
    g_free(s->api_key);
    g_free(s->language);
    g_free(s->keyterms);
    g_free(s->replacements);
    g_free(s->overlay_position);
    g_free(s->shortcut);
    for (int i = 0; i < 4; i++)
        g_free(s->models[i]);
}
void settings_copy(Settings *to, const Settings *s) {
    *to = *s;
    to->api_key = g_strdup(s->api_key);
    to->language = g_strdup(s->language);
    to->keyterms = g_strdup(s->keyterms);
    to->replacements = g_strdup(s->replacements);
    to->overlay_position = g_strdup(s->overlay_position);
    to->shortcut = g_strdup(s->shortcut);
    for (int i = 0; i < 4; i++)
        to->models[i] = g_strdup(s->models[i]);
}
static void read_string(GKeyFile *k, const char *name, char **dest) {
    char *v = g_key_file_get_string(k, "settings", name, NULL);
    if (v) {
        g_free(*dest);
        *dest = v;
    }
}
static gboolean load_legacy(Settings *s, const char *path, GError **error) {
    g_autoptr(GKeyFile) k = g_key_file_new();
    if (!g_key_file_load_from_file(k, path, G_KEY_FILE_NONE, error))
        return FALSE;
    read_string(k, "api_key", &s->api_key);
    read_string(k, "language", &s->language);
    read_string(k, "openai_model", &s->models[1]);
    read_string(k, "elevenlabs_model", &s->models[2]);
    read_string(k, "groq_model", &s->models[3]);
    read_string(k, "keyterms", &s->keyterms);
    read_string(k, "replacements", &s->replacements);
    read_string(k, "overlay_position", &s->overlay_position);
    read_string(k, "shortcut", &s->shortcut);
    if (g_key_file_has_key(k, "settings", "no_verbatim", NULL))
        s->no_verbatim = g_key_file_get_boolean(k, "settings", "no_verbatim", NULL);
    if (g_key_file_has_key(k, "settings", "mute_during_recording", NULL))
        s->mute_during_recording =
            g_key_file_get_boolean(k, "settings", "mute_during_recording", NULL);
    s->shortcuts_enabled = g_key_file_get_boolean(k, "settings", "shortcuts_enabled", NULL);
    if (g_key_file_has_key(k, "settings", "sound_volume", NULL))
        s->sound_volume =
            CLAMP(g_key_file_get_integer(k, "settings", "sound_volume", NULL), 0, 200);
    if (g_key_file_has_key(k, "settings", "auto_send", NULL))
        s->auto_send = g_key_file_get_boolean(k, "settings", "auto_send", NULL);
    int limit = g_key_file_get_integer(k, "settings", "limit_minutes", NULL);
    if (limit == 10 || limit == 20 || limit == 30 || limit == 60)
        s->limit_minutes = limit;
    int hours = g_key_file_get_integer(k, "settings", "retention_hours", NULL);
    if (hours == 1 || hours == 24 || hours == 168 || hours == 720)
        s->retention_hours = hours;
    // Legacy INI allowed commas as separators. Canonical TOML arrays do not:
    // a comma inside one recognition word must remain part of that word.
    g_auto(GStrv) terms = g_strsplit_set(s->keyterms, ",\n", -1);
    g_autoptr(GString) normalized = g_string_new(NULL);
    for (guint i = 0; terms[i]; i++) {
        char *term = g_strstrip(terms[i]);
        if (*term) {
            if (normalized->len) g_string_append_c(normalized, '\n');
            g_string_append(normalized, term);
        }
    }
    g_free(s->keyterms);
    s->keyterms = g_strdup(normalized->str);
    return TRUE;
}

char *settings_path(const char *dir) {
    const char *names[] = {"config.toml", "config.ini", "settings.ini"};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        char *path = g_build_filename(dir, names[i], NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS))
            return path;
        g_free(path);
    }
    return g_build_filename(dir, "config.toml", NULL);
}

static gboolean optional_value(toml_datum_t table, const char *key, toml_type_t type,
                               toml_datum_t *value, GError **error) {
    *value = toml_get(table, key);
    if (value->type != TOML_UNKNOWN && value->type != type) {
        INVALID("Invalid type for setting '%s'", key);
        return FALSE;
    }
    return TRUE;
}

static gboolean read_toml_string(toml_datum_t table, const char *key, char **dest,
                                 GError **error) {
    toml_datum_t value;
    if (!optional_value(table, key, TOML_STRING, &value, error))
        return FALSE;
    if (value.type == TOML_UNKNOWN)
        return TRUE;
    if ((int)strlen(value.u.s) != value.u.str.len || strpbrk(value.u.s, "\r\n")) {
        INVALID("Setting '%s' must be a single-line string without NUL", key);
        return FALSE;
    }
    g_free(*dest);
    *dest = g_strdup(value.u.s);
    return TRUE;
}

static gboolean read_toml_bool(toml_datum_t table, const char *key, gboolean *dest,
                               GError **error) {
    toml_datum_t value;
    if (!optional_value(table, key, TOML_BOOLEAN, &value, error))
        return FALSE;
    if (value.type != TOML_UNKNOWN)
        *dest = value.u.boolean;
    return TRUE;
}

static gboolean read_toml_int(toml_datum_t table, const char *key, int *dest,
                              GError **error) {
    toml_datum_t value;
    if (!optional_value(table, key, TOML_INT64, &value, error))
        return FALSE;
    if (value.type == TOML_UNKNOWN)
        return TRUE;
    if (value.u.int64 < G_MININT || value.u.int64 > G_MAXINT) {
        INVALID("Setting '%s' is outside the integer range", key);
        return FALSE;
    }
    *dest = (int)value.u.int64;
    return TRUE;
}

static gboolean read_toml_array(toml_datum_t table, const char *key, char **dest,
                                GError **error) {
    toml_datum_t value;
    if (!optional_value(table, key, TOML_ARRAY, &value, error))
        return FALSE;
    if (value.type == TOML_UNKNOWN)
        return TRUE;
    if (g_str_equal(key, "keyterms") && value.u.arr.size > 100) {
        INVALID("Setting 'keyterms' allows at most 100 words");
        return FALSE;
    }
    g_autoptr(GString) joined = g_string_new(NULL);
    for (int i = 0; i < value.u.arr.size; i++) {
        toml_datum_t item = value.u.arr.elem[i];
        if (item.type != TOML_STRING || (int)strlen(item.u.s) != item.u.str.len ||
            strpbrk(item.u.s, "\r\n")) {
            INVALID("Setting '%s' requires an array of single-line strings without NUL", key);
            return FALSE;
        }
        if (i) g_string_append_c(joined, '\n');
        g_string_append(joined, item.u.s);
    }
    g_free(*dest);
    *dest = g_strdup(joined->str);
    return TRUE;
}

static gboolean valid_settings(const Settings *s, GError **error) {
    if (s->sound_volume < 0 || s->sound_volume > 200) {
        INVALID("'recording_start_sound_volume' must be 0..200 percent");
        return FALSE;
    }
    if (s->limit_minutes != 10 && s->limit_minutes != 20 &&
        s->limit_minutes != 30 && s->limit_minutes != 60) {
        INVALID("'recording_time_limit_minutes' must be 10, 20, 30, or 60");
        return FALSE;
    }
    if (s->retention_hours != 1 && s->retention_hours != 24 &&
        s->retention_hours != 168 && s->retention_hours != 720) {
        INVALID("'log_retention_hours' must be 1, 24, 168, or 720");
        return FALSE;
    }
    const char *positions[] = {"hidden", "top_left", "top_center", "top_right", "center",
                              "bottom_left", "bottom_center", "bottom_right"};
    gboolean known = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(positions); i++)
        known |= g_str_equal(s->overlay_position, positions[i]);
    if (!known) {
        INVALID("'overlay_position' must be a documented widget position");
        return FALSE;
    }
    const char *strings[] = {s->api_key, s->language, s->models[1], s->models[2], s->models[3],
                            s->shortcut, s->keyterms, s->replacements};
    for (guint i = 0; i < G_N_ELEMENTS(strings); i++) {
        if (!g_utf8_validate(strings[i], -1, NULL)) {
            INVALID("Settings strings must be valid UTF-8");
            return FALSE;
        }
    }
    const char *single_line[] = {s->api_key, s->language, s->models[1], s->models[2], s->models[3], s->shortcut};
    for (guint i = 0; i < G_N_ELEMENTS(single_line); i++) {
        if (strpbrk(single_line[i], "\r\n")) {
            INVALID("Settings identifiers and API keys must be single-line strings");
            return FALSE;
        }
    }
    if (strchr(s->keyterms, '\r') || strchr(s->replacements, '\r')) {
        INVALID("Word and replacement lines must use LF, not CR");
        return FALSE;
    }
    g_auto(GStrv) words = g_strsplit(s->keyterms, "\n", -1);
    if (*s->keyterms && g_strv_length(words) > 100) {
        INVALID("Setting 'keyterms' allows at most 100 words");
        return FALSE;
    }
    return TRUE;
}

static gboolean load_toml(Settings *s, const char *path, GError **error) {
    g_autofree char *text = NULL;
    gsize length;
    if (!g_file_get_contents(path, &text, &length, error))
        return FALSE;
    if (length > G_MAXINT || !g_utf8_validate(text, length, NULL)) {
        INVALID("Configuration must be a valid UTF-8 TOML file");
        return FALSE;
    }
    toml_result_t parsed = toml_parse(text, (int)length);
    if (!parsed.ok) {
        INVALID("Invalid TOML configuration: %s", parsed.errmsg);
        toml_free(parsed);
        return FALSE;
    }
    toml_datum_t table = parsed.toptab;
    gboolean ok = read_toml_string(table, "shortcut", &s->shortcut, error) &&
        read_toml_string(table, "language", &s->language, error) &&
        read_toml_string(table, "openai_model", &s->models[1], error) &&
        read_toml_string(table, "elevenlabs_model", &s->models[2], error) &&
        read_toml_string(table, "groq_model", &s->models[3], error) &&
        read_toml_string(table, "overlay_position", &s->overlay_position, error) &&
        read_toml_bool(table, "auto_send", &s->auto_send, error) &&
        read_toml_bool(table, "no_verbatim", &s->no_verbatim, error) &&
        read_toml_bool(table, "mute_during_recording", &s->mute_during_recording, error) &&
        read_toml_bool(table, "shortcuts_enabled", &s->shortcuts_enabled, error) &&
        read_toml_int(table, "recording_start_sound_volume", &s->sound_volume, error) &&
        read_toml_int(table, "recording_time_limit_minutes", &s->limit_minutes, error) &&
        read_toml_int(table, "log_retention_hours", &s->retention_hours, error) &&
        read_toml_array(table, "keyterms", &s->keyterms, error) &&
        read_toml_array(table, "replacements", &s->replacements, error);
    if (ok && toml_get(table, "api_key").type != TOML_UNKNOWN) {
        INVALID("Store 'api_key' in user_config.json, not config.toml");
        ok = FALSE;
    }
    if (ok) ok = valid_settings(s, error);
    toml_free(parsed);
    return ok;
}

static JsonNode *read_user(const char *dir, GError **error) {
    g_autofree char *path = g_build_filename(dir, "user_config.json", NULL);
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        JsonNode *node = json_node_new(JSON_NODE_OBJECT);
        json_node_take_object(node, json_object_new());
        return node;
    }
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) parse_error = NULL;
    if (!json_parser_load_from_file(parser, path, &parse_error)) {
        INVALID("Cannot parse user_config.json; existing credentials were not changed");
        return NULL;
    }
    JsonNode *node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(node)) {
        INVALID("user_config.json must contain an object");
        return NULL;
    }
    JsonObject *object = json_node_get_object(node);
    if (json_object_has_member(object, "api_key")) {
        JsonNode *key = json_object_get_member(object, "api_key");
        if (!JSON_NODE_HOLDS_VALUE(key) || json_node_get_value_type(key) != G_TYPE_STRING) {
            INVALID("user_config.json 'api_key' must be a string");
            return NULL;
        }
        const char *value = json_node_get_string(key);
        if (strpbrk(value, "\r\n")) {
            INVALID("user_config.json 'api_key' must be a single-line string");
            return NULL;
        }
    }
    return json_node_copy(node);
}

gboolean settings_load(Settings *s, const char *dir, GError **error) {
    g_autofree char *path = settings_path(dir);
    gboolean exists = g_file_test(path, G_FILE_TEST_EXISTS);
    gboolean canonical = g_str_has_suffix(path, "config.toml");
    Settings next;
    settings_copy(&next, s);
    gboolean ok = !exists || (canonical ? load_toml(&next, path, error)
                                        : load_legacy(&next, path, error));
    if (ok) {
        g_autoptr(JsonNode) user = read_user(dir, error);
        ok = user != NULL;
        if (ok && json_object_has_member(json_node_get_object(user), "api_key")) {
            g_free(next.api_key);
            next.api_key = g_strdup(json_object_get_string_member(json_node_get_object(user), "api_key"));
        }
    }
    // Write credentials first, then commit TOML. Keep original INI for recovery.
    // Do this before consulting environment keys, which must stay in memory.
    if (ok && (!exists || !canonical))
        ok = settings_save(&next, dir, error);
    if (!ok) {
        settings_clear(&next);
        return FALSE;
    }
    if (!*next.api_key) {
        const char *names[] = {"ELEVENLABS_API_KEY", "GROQ_API_KEY", "OPENAI_API_KEY"};
        for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
            const char *key = g_getenv(names[i]);
            if (key && *key) {
                g_free(next.api_key);
                next.api_key = g_strdup(key);
                break;
            }
        }
    }
    settings_clear(s);
    *s = next;
    return TRUE;
}

static void append_quoted(GString *out, const char *value) {
    g_string_append_c(out, '"');
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
        case '"': g_string_append(out, "\\\""); break;
        case '\\': g_string_append(out, "\\\\"); break;
        case '\n': g_string_append(out, "\\n"); break;
        case '\r': g_string_append(out, "\\r"); break;
        case '\t': g_string_append(out, "\\t"); break;
        case '\b': g_string_append(out, "\\b"); break;
        case '\f': g_string_append(out, "\\f"); break;
        default:
            if (*p < 0x20 || *p == 0x7f)
                g_string_append_printf(out, "\\u%04x", *p);
            else
                g_string_append_c(out, *p);
        }
    }
    g_string_append_c(out, '"');
}

static void append_string(GString *out, const char *key, const char *value, const char *comment) {
    g_string_append_printf(out, "\n# %s\n%s = ", comment, key);
    append_quoted(out, value);
    g_string_append_c(out, '\n');
}

static void append_array(GString *out, const char *key, const char *value, const char *comment) {
    g_string_append_printf(out, "\n# %s\n%s = [", comment, key);
    if (*value) {
        g_auto(GStrv) entries = g_strsplit(value, "\n", -1);
        for (guint i = 0; entries[i]; i++) {
            if (i) g_string_append(out, ", ");
            append_quoted(out, entries[i]);
        }
    }
    g_string_append(out, "]\n");
}

static gboolean write_private(const char *path, const char *data, gsize length, GError **error) {
    if (!g_file_set_contents_full(path, data, length,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error))
        return FALSE;
    if (g_chmod(path, 0600) < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot secure settings file permissions");
        return FALSE;
    }
    return TRUE;
}

gboolean settings_save(const Settings *s, const char *dir, GError **error) {
    if (!valid_settings(s, error))
        return FALSE;
    g_autoptr(JsonNode) user = read_user(dir, error);
    if (!user)
        return FALSE;
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot create settings directory");
        return FALSE;
    }
    json_object_set_string_member(json_node_get_object(user), "api_key", s->api_key);
    g_autoptr(JsonGenerator) generator = json_generator_new();
    json_generator_set_root(generator, user);
    json_generator_set_pretty(generator, TRUE);
    gsize json_length;
    g_autofree char *json = json_generator_to_data(generator, &json_length);
    g_autofree char *user_path = g_build_filename(dir, "user_config.json", NULL);
    if (!write_private(user_path, json, json_length, error))
        return FALSE;
    g_autoptr(GString) out = g_string_new(
        "# KeyScribe preferences / 에이전트 설정 안내\n"
        "# Offline guide: <install prefix>/share/doc/keyscribe/readme.txt\n"
        "# Find this file with keyscribe --config-path.\n"
        "# Quit before external edits; relaunch afterward to apply them.\n"
        "# GUI saves rewrite values and regenerate standard comments.\n"
        "# Top-level TOML keys, double-quoted strings, true/false, integers and string arrays.\n"
        "# API key is stored separately in user_config.json; never put it in this file.\n");
    append_string(out, "shortcut", s->shortcut,
        "Recording shortcut (Linux portal accelerator), default CTRL+ALT+space; portal approval may be required. "
        "Tap it to record until the next press; hold it over a second to stop on release.");
    g_string_append_printf(out,
        "\n# Recording limit, minutes: 10, 20, 30, 60. Default 30.\nrecording_time_limit_minutes = %d\n"
        "\n# Log/recording retention, hours: 1, 24, 168, 720. Default 168.\nlog_retention_hours = %d\n"
        "\n# Press Enter after pasting. Boolean true/false, default true.\nauto_send = %s\n",
        s->limit_minutes, s->retention_hours, s->auto_send ? "true" : "false");
    append_string(out, "language", s->language, "Transcription language code, e.g. ko, en, ja. Default ko.");
    append_array(out, "keyterms", s->keyterms,
        "Recognition words: string array, at most 100 words, default []. Commas inside a word are literal.");
    append_array(out, "replacements", s->replacements,
        "Ordered find => replace string array, default []. Key tokens like [enter] execute keystrokes.");
    g_string_append_printf(out,
        "\n# Remove filler words on ElevenLabs scribe_v2/scribe_v2_medical. Boolean, default true.\nno_verbatim = %s\n"
        "\n# Mute system output during recording, restore afterward. Boolean, default true.\nmute_during_recording = %s\n"
        "\n# Start sound volume, integer 0..200 percent. 0 disables it, default 100; system output mute also silences it.\nrecording_start_sound_volume = %d\n",
        s->no_verbatim ? "true" : "false", s->mute_during_recording ? "true" : "false", s->sound_volume);
    append_string(out, "overlay_position", s->overlay_position,
        "Widget: hidden, top_left, top_center, top_right, center, bottom_left, bottom_center, bottom_right. Default bottom_center.");
    append_string(out, "openai_model", s->models[1], "OpenAI model name, default gpt-4o-mini-transcribe on Linux.");
    append_string(out, "elevenlabs_model", s->models[2], "ElevenLabs model name, default scribe_v2.");
    append_string(out, "groq_model", s->models[3], "Groq model name, default whisper-large-v3-turbo.");
    g_string_append_printf(out,
        "\n# Linux-only app-managed shortcut authorization state. Boolean, default false; do not edit to grant permissions.\nshortcuts_enabled = %s\n",
        s->shortcuts_enabled ? "true" : "false");
    g_autofree char *path = g_build_filename(dir, "config.toml", NULL);
    return write_private(path, out->str, out->len, error);
}
