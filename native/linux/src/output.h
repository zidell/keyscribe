#pragma once
#include <gio/gio.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
typedef struct {
    pa_context *context;
    gboolean wanted, held, prior_muted;
    guint32 sink;
    pa_cvolume volume;
    guint fade_timer, fade_step, restore_attempts;
} Output;
void output_init(Output *out, pa_glib_mainloop *loop);
void output_mute(Output *out);
void output_restore(Output *out);
void output_close(Output *out);
void sound_play(gboolean limit, int volume, GAsyncReadyCallback done, gpointer user);
