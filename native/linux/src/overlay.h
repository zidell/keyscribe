#pragma once
#include <gio/gio.h>
typedef struct {
    GSubprocess *process;
    char *executable;
} Overlay;
void overlay_init(Overlay *overlay);
void overlay_update(Overlay *overlay, const char *position, const char *text, double level,
                    gboolean warning);
void overlay_close(Overlay *overlay);
int overlay_run(void);
