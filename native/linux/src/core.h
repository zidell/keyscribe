#pragma once
#include <gio/gio.h>
#include <stdio.h>

typedef enum { PROVIDER_NONE, PROVIDER_OPENAI, PROVIDER_ELEVENLABS, PROVIDER_GROQ } Provider;
typedef struct {
    char *api_key, *language, *models[4], *keyterms, *replacements, *overlay_position, *shortcut;
    gboolean auto_send, no_verbatim, mute_during_recording, shortcuts_enabled;
    int limit_minutes, retention_hours, sound_volume;
} Settings;
void settings_init(Settings *s);
void settings_clear(Settings *s);
void settings_copy(Settings *to, const Settings *from);
char *settings_path(const char *dir);
gboolean settings_load(Settings *s, const char *dir, GError **error);
gboolean settings_save(const Settings *s, const char *dir, GError **error);
Provider provider_from_key(const char *key);
char *clean_text(const char *text);
char *apply_replacements(const char *text, const char *rules);
gboolean wav_header(FILE *file, guint32 bytes);
char *transcribe(const char *path, const Settings *s, GCancellable *cancel, GError **error);
void prune_recordings(const char *dir, int hours);
char **fetch_models(const char *api_key, GCancellable *cancel, GError **error);

void debug_log(const char *event);
void debug_log_init(const char *dir);
typedef struct {
    char *text;
    guint keys[8];
    guint n_keys;
} PasteSegment;
GPtrArray *paste_segments(const char *text);
