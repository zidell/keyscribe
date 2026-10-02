#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FAIL(...) g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, __VA_ARGS__)
Provider provider_from_key(const char *key) {
    if (g_str_has_prefix(key, "gsk_"))
        return PROVIDER_GROQ;
    if (g_str_has_prefix(key, "sk-"))
        return PROVIDER_OPENAI;
    if (g_str_has_prefix(key, "sk_"))
        return PROVIDER_ELEVENLABS;
    return PROVIDER_NONE;
}
void settings_init(Settings *s) {
    *s = (Settings){.api_key = g_strdup(""),
                    .language = g_strdup("ko"),
                    .models = {g_strdup(""), g_strdup("gpt-4o-mini-transcribe"),
                               g_strdup("scribe_v2"), g_strdup("whisper-large-v3-turbo")},
                    .keyterms = g_strdup(""),
                    .replacements = g_strdup(""),
                    .hold = TRUE,
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
gboolean settings_load(Settings *s, const char *dir, GError **error) {
    g_autofree char *path = g_build_filename(dir, "settings.ini", NULL);
    g_autoptr(GKeyFile) k = g_key_file_new();
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
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
        if (g_key_file_has_key(k, "settings", "hold", NULL))
            s->hold = g_key_file_get_boolean(k, "settings", "hold", NULL);
        if (g_key_file_has_key(k, "settings", "auto_send", NULL))
            s->auto_send = g_key_file_get_boolean(k, "settings", "auto_send", NULL);
        int limit = g_key_file_get_integer(k, "settings", "limit_minutes", NULL);
        if (limit == 10 || limit == 20 || limit == 30 || limit == 60)
            s->limit_minutes = limit;
        int hours = g_key_file_get_integer(k, "settings", "retention_hours", NULL);
        if (hours == 1 || hours == 24 || hours == 168 || hours == 720)
            s->retention_hours = hours;
    }
    if (!*s->api_key) {
        const char *names[] = {"ELEVENLABS_API_KEY", "GROQ_API_KEY", "OPENAI_API_KEY"};
        for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
            const char *v = g_getenv(names[i]);
            if (v && *v) {
                g_free(s->api_key);
                s->api_key = g_strdup(v);
                break;
            }
        }
    }
    return TRUE;
}
gboolean settings_save(const Settings *s, const char *dir, GError **error) {
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        FAIL("설정 폴더를 만들 수 없습니다");
        return FALSE;
    }
    g_autoptr(GKeyFile) k = g_key_file_new();
#define STR(name, value) g_key_file_set_string(k, "settings", name, value)
    STR("api_key", s->api_key);
    STR("language", s->language);
    STR("openai_model", s->models[1]);
    STR("elevenlabs_model", s->models[2]);
    STR("groq_model", s->models[3]);
    STR("keyterms", s->keyterms);
    STR("replacements", s->replacements);
    STR("overlay_position", s->overlay_position);
    STR("shortcut", s->shortcut);
    g_key_file_set_boolean(k, "settings", "no_verbatim", s->no_verbatim);
    g_key_file_set_boolean(k, "settings", "mute_during_recording", s->mute_during_recording);
    g_key_file_set_boolean(k, "settings", "shortcuts_enabled", s->shortcuts_enabled);
    g_key_file_set_integer(k, "settings", "sound_volume", s->sound_volume);
    g_key_file_set_boolean(k, "settings", "hold", s->hold);
    g_key_file_set_boolean(k, "settings", "auto_send", s->auto_send);
    g_key_file_set_integer(k, "settings", "limit_minutes", s->limit_minutes);
    g_key_file_set_integer(k, "settings", "retention_hours", s->retention_hours);
    g_key_file_set_comment(k, NULL, NULL,
        "KeyScribe preferences / 에이전트 설정 안내\n"
        "Guide: https://github.com/zidell/keyscribe/blob/main/docs/agent-settings.md\n"
        "Quit the app before external edits; relaunch afterward to apply them.\n"
        "Saving in Settings does not reload external edits and rewrites this file.\n"
        "Use [settings], unquoted strings, true/false, and integers.\n"
        "Multiline values use escaped newlines (\\n).\n"
        "This file includes the API key: keep it and backups private (0600).",
        NULL);
    gsize size;
    g_autofree char *data = g_key_file_to_data(k, &size, NULL);
    g_autofree char *path = g_build_filename(dir, "settings.ini", NULL);
    if (!g_file_set_contents_full(path, data, size,
                                  G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
                                  0600, error))
        return FALSE;
    if (g_chmod(path, 0600) < 0) {
        FAIL("설정 파일 권한을 설정할 수 없습니다");
        return FALSE;
    }
    return TRUE;
}
char *clean_text(const char *text) {
    GString *out = g_string_new(NULL);
    while (*text) {
        if (*text == '[') {
            const char *end = strchr(text, ']');
            if (end) {
                text = end + 1;
                continue;
            }
        }
        gunichar c = g_utf8_get_char(text);
        if (g_unichar_isspace(c)) {
            if (out->len && out->str[out->len - 1] != ' ')
                g_string_append_c(out, ' ');
        } else
            g_string_append_unichar(out, c);
        text = g_utf8_next_char(text);
    }
    g_strchomp(out->str);
    return g_string_free(out, FALSE);
}
char *apply_replacements(const char *text, const char *rules) {
    char *result = g_strdup(text);
    g_auto(GStrv) lines = g_strsplit(rules, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *sep = strstr(lines[i], "=>");
        if (!sep)
            sep = strstr(lines[i], "->");
        if (!sep)
            continue;
        *sep = 0;
        char *from = g_strstrip(lines[i]), *to = g_strstrip(sep + 2);
        if (!*from)
            continue;
        g_auto(GStrv) parts = g_strsplit(result, from, -1);
        g_free(result);
        result = g_strjoinv(to, parts);
    }
    return result;
}
static void le16(unsigned char *p, guint16 n) {
    p[0] = n;
    p[1] = n >> 8;
}
static void le32(unsigned char *p, guint32 n) {
    for (int i = 0; i < 4; i++)
        p[i] = n >> (8 * i);
}
gboolean wav_header(FILE *file, guint32 bytes) {
    unsigned char h[44] = {0};
    memcpy(h, "RIFF", 4);
    le32(h + 4, bytes + 36);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    le16(h + 20, 1);
    le16(h + 22, 1);
    le32(h + 24, 16000);
    le32(h + 28, 32000);
    le16(h + 32, 2);
    le16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    le32(h + 40, bytes);
    return fseek(file, 0, SEEK_SET) == 0 && fwrite(h, 1, 44, file) == 44;
}
typedef struct {
    GString *body;
    GCancellable *cancel;
} Transfer;
static size_t receive(char *data, size_t size, size_t count, void *user) {
    Transfer *t = user;
    size_t bytes = size * count;
    if (t->body->len + bytes > 4 * 1024 * 1024)
        return 0;
    g_string_append_len(t->body, data, bytes);
    return bytes;
}
static int progress(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return g_cancellable_is_cancelled(((Transfer *)user)->cancel);
}
static void field(curl_mime *mime, const char *name, const char *value) {
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, name);
    curl_mime_data(part, value, CURL_ZERO_TERMINATED);
}
static char *transcribe_single(const char *path, const Settings *s, GCancellable *cancel,
                               GError **error) {
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return NULL;
    Provider p = provider_from_key(s->api_key);
    if (!p || strpbrk(s->api_key, "\r\n")) {
        FAIL("설정에서 OpenAI, ElevenLabs 또는 Groq API 키를 입력하세요");
        return NULL;
    }
#ifndef KEYSCRIBE_TEST_ENDPOINT
    const char *urls[] = {"", "https://api.openai.com/v1/audio/transcriptions",
                          "https://api.elevenlabs.io/v1/speech-to-text",
                          "https://api.groq.com/openai/v1/audio/transcriptions"};
#endif
    CURL *curl = curl_easy_init();
    if (!curl) {
        FAIL("네트워크 초기화 실패");
        return NULL;
    }
    curl_mime *mime = curl_mime_init(curl);
    field(mime, p == PROVIDER_ELEVENLABS ? "model_id" : "model", s->models[p]);
    if (*s->language)
        field(mime, p == PROVIDER_ELEVENLABS ? "language_code" : "language", s->language);
    if (*s->keyterms) {
        if (p == PROVIDER_ELEVENLABS) {
            g_auto(GStrv) terms = g_strsplit_set(s->keyterms, ",\n", -1);
            for (int i = 0; terms[i]; i++)
                if (*g_strstrip(terms[i]))
                    field(mime, "keyterms", terms[i]);
        } else {
            g_auto(GStrv) terms = g_strsplit_set(s->keyterms, ",\n", -1);
            for (int i = 0; terms[i]; i++)
                g_strstrip(terms[i]);
            g_autofree char *joined = g_strjoinv(", ", terms);
            g_autofree char *prompt = g_strdup_printf("고유명사 표기 참고: %s", joined);
            field(mime, "prompt", prompt);
        }
    }
    if (p == PROVIDER_ELEVENLABS &&
        (g_str_equal(s->models[p], "scribe_v2") || g_str_equal(s->models[p], "scribe_v2_medical")))
        field(mime, "no_verbatim", s->no_verbatim ? "true" : "false");
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, path);
    curl_mime_type(part, "audio/wav");
    g_autofree char *auth = g_strdup_printf(
        p == PROVIDER_ELEVENLABS ? "xi-api-key: %s" : "Authorization: Bearer %s", s->api_key);
    struct curl_slist *headers = curl_slist_append(NULL, auth);
    Transfer t = {g_string_new(NULL), cancel};
#ifdef KEYSCRIBE_TEST_ENDPOINT
    curl_easy_setopt(curl, CURLOPT_URL, KEYSCRIBE_TEST_ENDPOINT);
#else
    curl_easy_setopt(curl, CURLOPT_URL, urls[p]);
#endif
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    CURLcode code = CURLE_OK;
    long status = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        g_string_truncate(t.body, 0);
        code = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        g_autofree char *event = g_strdup_printf("transcription HTTP=%ld attempt=%d transport=%d",
                                                 status, attempt + 1, code);
        debug_log(event);
        if (g_cancellable_is_cancelled(cancel) ||
            (code == CURLE_OK && status != 408 && status != 429 && status < 500))
            break;
        if (attempt == 0)
            for (int i = 0; i < 10 && !g_cancellable_is_cancelled(cancel); i++)
                g_usleep(100000);
    }
    char *result = NULL;
    if (g_cancellable_set_error_if_cancelled(cancel, error)) {
    } else if (code != CURLE_OK)
        FAIL("전사 연결 실패: %s", curl_easy_strerror(code));
    else if (status < 200 || status >= 300)
        FAIL("전사 실패 (HTTP %ld). API 키·모델·사용 한도를 확인하세요. 녹음은 보관됩니다.",
             status);
    else {
        g_autoptr(JsonParser) parser = json_parser_new();
        if (json_parser_load_from_data(parser, t.body->str, t.body->len, error)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonNode *node = JSON_NODE_HOLDS_OBJECT(root)
                                 ? json_object_get_member(json_node_get_object(root), "text")
                                 : NULL;
            if (node && JSON_NODE_HOLDS_VALUE(node) &&
                json_node_get_value_type(node) == G_TYPE_STRING) {
                result = clean_text(json_node_get_string(node));
                if (p != PROVIDER_ELEVENLABS && *s->keyterms) {
                    g_auto(GStrv) terms = g_strsplit_set(s->keyterms, ",\n", -1);
                    for (int i = 0; terms[i]; i++)
                        g_strstrip(terms[i]);
                    g_autofree char *joined = g_strjoinv(", ", terms);
                    g_autofree char *echo = g_strdup_printf("고유명사 표기 참고: %s", joined);
                    g_autofree char *candidate = g_strdup(result);
                    gsize n = strlen(candidate);
                    while (n && strchr(".! ", candidate[n - 1]))
                        candidate[--n] = 0;
                    if (g_str_equal(candidate, echo)) {
                        g_free(result);
                        result = g_strdup("");
                    }
                }
            } else
                FAIL("전사 응답에 텍스트가 없습니다");
        }
    }
    g_string_free(t.body, TRUE);
    curl_slist_free_all(headers);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    return result;
}
char *transcribe(const char *path, const Settings *s, GCancellable *cancel, GError **error) {
    GStatBuf st;
    if (g_stat(path, &st) < 0) {
        FAIL("녹음 파일을 읽을 수 없습니다");
        return NULL;
    }
    if (st.st_size <= 24 * 1024 * 1024 || provider_from_key(s->api_key) == PROVIDER_ELEVENLABS)
        return transcribe_single(path, s, cancel, error);
    FILE *in = fopen(path, "rb");
    if (!in) {
        FAIL("녹음 파일을 열 수 없습니다");
        return NULL;
    }
    fseek(in, 44, SEEK_SET);
    GString *all = g_string_new(NULL);
    char buffer[32000];
    gboolean ok = TRUE;
    while (!feof(in) && ok) {
        if (g_cancellable_set_error_if_cancelled(cancel, error)) {
            ok = FALSE;
            break;
        }
        g_autofree char *tmp = NULL;
        int fd = g_file_open_tmp("keyscribe-part-XXXXXX.wav", &tmp, error);
        if (fd < 0) {
            ok = FALSE;
            break;
        }
        FILE *out = fdopen(fd, "wb");
        if (!out) {
            close(fd);
            g_unlink(tmp);
            FAIL("임시 녹음을 만들 수 없습니다");
            ok = FALSE;
            break;
        }
        guint32 bytes = 0;
        ok = wav_header(out, 0);
        for (int seconds = 0; seconds < 9 * 60 && ok; seconds++) {
            size_t n = fread(buffer, 1, sizeof(buffer), in);
            if (!n)
                break;
            ok = fwrite(buffer, 1, n, out) == n;
            bytes += n;
        }
        if (ferror(in))
            ok = FALSE;
        if (!wav_header(out, bytes))
            ok = FALSE;
        if (fclose(out) != 0)
            ok = FALSE;
        if (!ok) {
            FAIL("녹음 분할 중 파일 오류");
            g_unlink(tmp);
            break;
        }
        if (bytes) {
            g_autofree char *text = transcribe_single(tmp, s, cancel, error);
            if (!text)
                ok = FALSE;
            else if (*text) {
                if (all->len)
                    g_string_append_c(all, ' ');
                g_string_append(all, text);
            }
        }
        g_unlink(tmp);
    }
    fclose(in);
    if (!ok) {
        g_string_free(all, TRUE);
        return NULL;
    }
    return g_string_free(all, FALSE);
}
void prune_recordings(const char *dir, int hours) {
    g_autofree char *log = g_build_filename(dir, "debug.log", NULL);
    char *contents = NULL;
    gsize length;
    if (g_file_get_contents(log, &contents, &length, NULL)) {
        g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
        g_free(contents);
        GString *kept = g_string_new(NULL);
        gint64 cutoff_ms = g_get_real_time() / 1000 - (gint64)hours * 3600 * 1000;
        for (int i = 0; lines[i]; i++)
            if (g_ascii_strtoll(lines[i], NULL, 10) >= cutoff_ms) {
                g_string_append(kept, lines[i]);
                g_string_append_c(kept, '\n');
            }
        g_file_set_contents_full(log, kept->str, kept->len, G_FILE_SET_CONTENTS_CONSISTENT, 0600,
                                 NULL);
        g_string_free(kept, TRUE);
    }

    g_autoptr(GDir) d = g_dir_open(dir, 0, NULL);
    if (!d)
        return;
    gint64 cutoff = g_get_real_time() / G_USEC_PER_SEC - (gint64)hours * 3600;
    const char *name;
    while ((name = g_dir_read_name(d))) {
        if (!g_str_has_prefix(name, "recording-") || !g_str_has_suffix(name, ".wav"))
            continue;
        g_autofree char *path = g_build_filename(dir, name, NULL);
        GStatBuf st;
        if (g_lstat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_mtime < cutoff)
            g_unlink(path);
    }
}

static gboolean transcription_model(Provider p, const char *name) {
    gboolean supported = p == PROVIDER_ELEVENLABS ? g_str_has_prefix(name, "scribe")
                         : p == PROVIDER_GROQ
                             ? g_str_has_prefix(name, "whisper-")
                             : (strstr(name, "transcribe") || g_str_equal(name, "whisper-1"));
    return supported && !strstr(name, "realtime") &&
           !g_regex_match_simple("-[0-9]{4}-[0-9]{2}-[0-9]{2}$", name, 0, 0);
}
static gint compare_models(gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(char *const *)a, *(char *const *)b);
}
char **fetch_models(const char *api_key, GCancellable *cancel, GError **error) {
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return NULL;
    Provider p = provider_from_key(api_key);
    if (!p || strpbrk(api_key, "\r\n")) {
        FAIL("API 키 형식을 확인하세요 (sk-, sk_, gsk_)");
        return NULL;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        FAIL("네트워크 초기화 실패");
        return NULL;
    }
    g_autofree char *auth = g_strdup_printf(
        p == PROVIDER_ELEVENLABS ? "xi-api-key: %s" : "Authorization: Bearer %s", api_key);
    struct curl_slist *headers = curl_slist_append(NULL, auth);
    Transfer t = {g_string_new(NULL), cancel};
#ifdef KEYSCRIBE_TEST_ENDPOINT
    curl_easy_setopt(curl, CURLOPT_URL, KEYSCRIBE_TEST_ENDPOINT);
#else
    const char *urls[] = {"", "https://api.openai.com/v1/models",
                          "https://api.elevenlabs.io/v1/models",
                          "https://api.groq.com/openai/v1/models"};
    curl_easy_setopt(curl, CURLOPT_URL, urls[p]);
#endif
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    char **result = NULL;
    if (g_cancellable_set_error_if_cancelled(cancel, error)) {
    } else if (code != CURLE_OK)
        FAIL("모델 목록 연결 실패: %s", curl_easy_strerror(code));
    else if (status < 200 || status >= 300)
        FAIL("모델 목록 요청 실패 (HTTP %ld). API 키와 권한을 확인하세요.", status);
    else {
        g_autoptr(JsonParser) parser = json_parser_new();
        if (json_parser_load_from_data(parser, t.body->str, t.body->len, error)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonNode *items =
                p == PROVIDER_ELEVENLABS
                    ? root
                    : (JSON_NODE_HOLDS_OBJECT(root)
                           ? json_object_get_member(json_node_get_object(root), "data")
                           : NULL);
            if (!items || !JSON_NODE_HOLDS_ARRAY(items))
                FAIL("모델 목록 응답 형식이 올바르지 않습니다");
            else {
                GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
                JsonArray *array = json_node_get_array(items);
                for (guint i = 0; i < json_array_get_length(array); i++) {
                    JsonNode *item = json_array_get_element(array, i);
                    JsonNode *id =
                        JSON_NODE_HOLDS_OBJECT(item)
                            ? json_object_get_member(json_node_get_object(item),
                                                     p == PROVIDER_ELEVENLABS ? "model_id" : "id")
                            : NULL;
                    if (id && JSON_NODE_HOLDS_VALUE(id) &&
                        json_node_get_value_type(id) == G_TYPE_STRING) {
                        const char *name = json_node_get_string(id);
                        if (transcription_model(p, name) &&
                            !g_ptr_array_find_with_equal_func(names, name, (GEqualFunc)g_str_equal,
                                                              NULL))
                            g_ptr_array_add(names, g_strdup(name));
                    }
                }
                if (!names->len) {
                    FAIL("이 API 키에서 사용할 수 있는 음성 인식 모델이 없습니다");
                    g_ptr_array_unref(names);
                } else {
                    g_ptr_array_sort(names, compare_models);
                    g_ptr_array_add(names, NULL);
                    result = (char **)g_ptr_array_free(names, FALSE);
                }
            }
        }
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    g_string_free(t.body, TRUE);
    return result;
}

static GMutex log_mutex;
static char *log_directory;
void debug_log_init(const char *dir) {
    g_free(log_directory);
    log_directory = g_strdup(dir);
}
void debug_log(const char *event) {
    if (!log_directory)
        return;
    g_autofree char *path = g_build_filename(log_directory, "debug.log", NULL);
    g_mutex_lock(&log_mutex);
    FILE *file = fopen(path, "a");
    if (file) {
        g_chmod(path, 0600);
        fprintf(file, "%" G_GINT64_FORMAT " pid=%ld %s\n", g_get_real_time() / 1000,
                (long)getpid(), event);
        fclose(file);
    }
    g_mutex_unlock(&log_mutex);
}

#include <X11/keysym.h>
static guint modifier_symbol(const char *name) {
    if (g_str_equal(name, "ctrl") || g_str_equal(name, "control") || g_str_equal(name, "cmd") ||
        g_str_equal(name, "command"))
        return XK_Control_L;
    if (g_str_equal(name, "alt") || g_str_equal(name, "option") || g_str_equal(name, "opt"))
        return XK_Alt_L;
    if (g_str_equal(name, "shift"))
        return XK_Shift_L;
    if (g_str_equal(name, "win") || g_str_equal(name, "super") || g_str_equal(name, "meta"))
        return XK_Super_L;
    return 0;
}
static guint key_symbol(const char *name) {
    const char *names[] = {"enter",     "return",  "tab",        "space",     "spacebar",
                           "esc",       "escape",  "backspace",  "delete",    "del",
                           "up",        "arrowup", "down",       "arrowdown", "left",
                           "arrowleft", "right",   "arrowright", "home",      "end",
                           "pageup",    "pgup",    "pagedown",   "pgdn",      "pgdown"};
    const guint codes[] = {XK_Return,  XK_Return,  XK_Tab,       XK_space,     XK_space,
                           XK_Escape,  XK_Escape,  XK_BackSpace, XK_Delete,    XK_Delete,
                           XK_Up,      XK_Up,      XK_Down,      XK_Down,      XK_Left,
                           XK_Left,    XK_Right,   XK_Right,     XK_Home,      XK_End,
                           XK_Page_Up, XK_Page_Up, XK_Page_Down, XK_Page_Down, XK_Page_Down};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++)
        if (g_str_equal(name, names[i]))
            return codes[i];
    if (name[0] == 'f') {
        char *end = NULL;
        int n = strtol(name + 1, &end, 10);
        if (end && !*end && n >= 1 && n <= 12)
            return XK_F1 + n - 1;
    }
    if (strlen(name) == 1 && g_ascii_isalnum(name[0]))
        return (guint)name[0];
    return 0;
}
static gboolean parse_key_token(const char *body, PasteSegment *segment) {
    GString *normalized = g_string_new(NULL);
    for (const char *p = body; *p; p++)
        if (!g_ascii_isspace(*p))
            g_string_append_c(normalized, g_ascii_tolower(*p));
    g_auto(GStrv) parts = g_strsplit_set(normalized->str, "+-", -1);
    g_string_free(normalized, TRUE);
    guint key = 0;
    gboolean single = FALSE;
    for (int i = 0; parts[i]; i++) {
        if (!*parts[i])
            continue;
        guint modifier = modifier_symbol(parts[i]);
        if (modifier) {
            gboolean seen = FALSE;
            for (guint n = 0; n < segment->n_keys; n++)
                if (segment->keys[n] == modifier)
                    seen = TRUE;
            if (!seen && segment->n_keys < 7)
                segment->keys[segment->n_keys++] = modifier;
        } else {
            if (key)
                return FALSE;
            key = key_symbol(parts[i]);
            if (!key)
                return FALSE;
            single = strlen(parts[i]) == 1;
        }
    }
    if (!key || (single && !segment->n_keys))
        return FALSE;
    segment->keys[segment->n_keys++] = key;
    return TRUE;
}
static void segment_free(void *data) {
    PasteSegment *s = data;
    g_free(s->text);
    g_free(s);
}
GPtrArray *paste_segments(const char *text) {
    GPtrArray *result = g_ptr_array_new_with_free_func(segment_free);
    GString *buffer = g_string_new(NULL);
    const char *rest = text;
    while (*rest) {
        if (*rest == '[') {
            const char *close = strchr(rest + 1, ']');
            if (close) {
                g_autofree char *body = g_strndup(rest + 1, close - rest - 1);
                PasteSegment *segment = g_new0(PasteSegment, 1);
                if (parse_key_token(body, segment)) {
                    if (buffer->len) {
                        PasteSegment *part = g_new0(PasteSegment, 1);
                        part->text = g_strdup(buffer->str);
                        g_ptr_array_add(result, part);
                        g_string_truncate(buffer, 0);
                    }
                    g_ptr_array_add(result, segment);
                    rest = close + 1;
                    continue;
                }
                g_free(segment);
            }
        }
        g_string_append_c(buffer, *rest++);
    }
    if (buffer->len) {
        PasteSegment *part = g_new0(PasteSegment, 1);
        part->text = g_strdup(buffer->str);
        g_ptr_array_add(result, part);
    }
    g_string_free(buffer, TRUE);
    return result;
}
