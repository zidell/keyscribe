#include "output.h"
#include "core.h"
#include <pulse/simple.h>
#include <math.h>
#include "sounds.h"
static gboolean retry_restore(void *user);
static void restored(pa_context *c, int success, void *user) {
    (void)c;
    Output *out = user;
    if (!success && ++out->restore_attempts < 3) {
        g_timeout_add(200, retry_restore, out);
        return;
    }
    debug_log(success ? "output mute restored" : "output mute restore failed");
    out->held = FALSE;
    if (out->wanted)
        output_mute(out);
}
void output_restore(Output *out) {
    out->wanted = FALSE;
    if (out->fade_timer) {
        g_source_remove(out->fade_timer);
        out->fade_timer = 0;
    }
    if (out->held && out->context && pa_context_get_state(out->context) == PA_CONTEXT_READY) {
        pa_operation *volume =
            pa_context_set_sink_volume_by_index(out->context, out->sink, &out->volume, NULL, NULL);
        if (volume)
            pa_operation_unref(volume);
        pa_operation *mute = pa_context_set_sink_mute_by_index(out->context, out->sink,
                                                               out->prior_muted, restored, out);
        if (mute)
            pa_operation_unref(mute);
        else
            out->held = FALSE;
    }
}
static gboolean retry_restore(void *user) {
    output_restore(user);
    return G_SOURCE_REMOVE;
}
static gboolean fade(void *user) {
    Output *out = user;
    if (!out->wanted || !out->context || pa_context_get_state(out->context) != PA_CONTEXT_READY) {
        out->fade_timer = 0;
        return G_SOURCE_REMOVE;
    }
    pa_cvolume volume = out->volume;
    ++out->fade_step;
    pa_cvolume_scale(&volume, pa_cvolume_max(&out->volume) * (5 - out->fade_step) / 5);
    pa_operation *op =
        pa_context_set_sink_volume_by_index(out->context, out->sink, &volume, NULL, NULL);
    if (op)
        pa_operation_unref(op);
    if (out->fade_step == 5) {
        op = pa_context_set_sink_mute_by_index(out->context, out->sink, TRUE, NULL, NULL);
        if (op)
            pa_operation_unref(op);
        op = pa_context_set_sink_volume_by_index(out->context, out->sink, &out->volume, NULL, NULL);
        if (op)
            pa_operation_unref(op);
        out->fade_timer = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
static void sink_info(pa_context *c, const pa_sink_info *info, int eol, void *user) {
    (void)c;
    Output *out = user;
    if (eol || !info || !out->wanted || out->held)
        return;
    out->sink = info->index;
    out->prior_muted = info->mute;
    out->volume = info->volume;
    out->held = TRUE;
    out->restore_attempts = 0;
    if (!info->mute) {
        out->fade_step = 0;
        out->fade_timer = g_timeout_add(25, fade, out);
    }
    debug_log(info->mute ? "output already muted" : "output fading to mute");
}
static void server_info(pa_context *c, const pa_server_info *info, void *user) {
    Output *out = user;
    if (!info || !out->wanted || out->held)
        return;
    pa_operation *op = pa_context_get_sink_info_by_name(c, info->default_sink_name, sink_info, out);
    if (op)
        pa_operation_unref(op);
}
void output_mute(Output *out) {
    out->wanted = TRUE;
    if (out->context && pa_context_get_state(out->context) == PA_CONTEXT_READY) {
        pa_operation *op = pa_context_get_server_info(out->context, server_info, out);
        if (op)
            pa_operation_unref(op);
    }
}
static void connected(pa_context *c, void *user) {
    Output *out = user;
    if (pa_context_get_state(c) == PA_CONTEXT_READY && out->wanted)
        output_mute(out);
}
void output_init(Output *out, pa_glib_mainloop *loop) {
    *out = (Output){0};
    out->context = pa_context_new(pa_glib_mainloop_get_api(loop), "KeyScribe output control");
    if (out->context) {
        pa_context_set_state_callback(out->context, connected, out);
        pa_context_connect(out->context, NULL, PA_CONTEXT_NOFLAGS, NULL);
    }
}
void output_close(Output *out) {
    output_restore(out);
    gint64 deadline = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    while (out->held && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    }
    if (out->context) {
        pa_context_set_state_callback(out->context, NULL, NULL);
        pa_context_disconnect(out->context);
        pa_context_unref(out->context);
        out->context = NULL;
    }
}
typedef struct {
    gboolean limit;
    int volume;
} Sound;
static void play_worker(GTask *task, void *source, void *data, GCancellable *cancel) {
    (void)source;
    (void)cancel;
    Sound *sound = data;
    const unsigned char *wav = sound->limit ? recording_limit : recording_start;
    size_t length = sound->limit ? sizeof(recording_limit) : sizeof(recording_start);
    unsigned char *pcm = g_memdup2(wav + 44, length - 44);
    double gain = CLAMP(sound->volume, 0, 200) / 100.0;
    for (size_t i = 0; i + 1 < length - 44; i += 2) {
        gint16 sample = (gint16)(pcm[i] | ((guint16)pcm[i + 1] << 8));
        double scaled = sample / 32768.0 * gain;
        double magnitude = fabs(scaled);
        double limited =
            magnitude <= 0.8 ? magnitude : 0.8 + 0.2 * (1 - exp(-(magnitude - 0.8) / 0.2));
        gint16 value = lrint(copysign(limited, scaled) * 32767);
        pcm[i] = value;
        pcm[i + 1] = ((guint16)value) >> 8;
    }
    pa_sample_spec spec = {PA_SAMPLE_S16LE, 24000, 1};
    int error;
    pa_simple *stream = pa_simple_new(NULL, "KeyScribe", PA_STREAM_PLAYBACK, NULL,
                                      "KeyScribe notification", &spec, NULL, NULL, &error);
    gboolean ok = stream && pa_simple_write(stream, pcm, length - 44, &error) >= 0 &&
                  pa_simple_drain(stream, &error) >= 0;
    if (stream)
        pa_simple_free(stream);
    g_free(pcm);
    g_task_return_boolean(task, ok);
}
void sound_play(gboolean limit, int volume, GAsyncReadyCallback done, gpointer user) {
    if (volume <= 0)
        return;
    Sound *sound = g_new0(Sound, 1);
    sound->limit = limit;
    sound->volume = volume;
    GTask *task = g_task_new(NULL, NULL, done, user);
    g_task_set_task_data(task, sound, g_free);
    g_task_run_in_thread(task, play_worker);
    g_object_unref(task);
}
