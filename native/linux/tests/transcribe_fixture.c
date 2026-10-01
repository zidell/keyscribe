#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <curl/curl.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Settings s;
    settings_init(&s);
    g_free(s.api_key);
    s.api_key = g_strdup(argv[1]);
    Provider p = provider_from_key(s.api_key);
    g_free(s.models[p]);
    s.models[p] = g_strdup(argv[2]);
    g_free(s.keyterms);
    s.keyterms = g_strdup("KeyScribe, 우분투");
    if (g_str_equal(argv[3], "models")) {
        g_autoptr(GCancellable) cancel = g_cancellable_new();
        g_autoptr(GError) error = NULL;
        g_auto(GStrv) names = fetch_models(s.api_key, cancel, &error);
        if (error)
            g_printerr("%s\n", error->message);
        else
            for (int i = 0; names[i]; i++)
                printf("%s\n", names[i]);
        settings_clear(&s);
        curl_global_cleanup();
        return error ? 1 : 0;
    }
    char *path = NULL;
    int fd = g_file_open_tmp("keyscribe-http-XXXXXX.wav", &path, NULL);
    FILE *f = fdopen(fd, "w+b");
    guint32 bytes = g_str_equal(argv[3], "large") ? 25 * 1024 * 1024 : 32000;
    wav_header(f, 0);
    char zero[32000] = {0};
    for (guint32 n = 0; n < bytes;) {
        guint32 size = MIN(bytes - n, sizeof(zero));
        fwrite(zero, 1, size, f);
        n += size;
    }
    wav_header(f, bytes);
    fclose(f);
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_autoptr(GError) error = NULL;
    g_autofree char *text = transcribe(path, &s, cancel, &error);
    if (error)
        g_printerr("%s\n", error->message);
    else
        // Machine-readable UTF-8 output must not depend on GLib's locale conversion.
        printf("%s\n", text);
    g_unlink(path);
    g_free(path);
    settings_clear(&s);
    curl_global_cleanup();
    return error ? 1 : 0;
}
