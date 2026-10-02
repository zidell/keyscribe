#include <gtk/gtk.h>
#include "portal.h"
#ifdef KEYSCRIBE_KDE_ESCAPE_TEST
#include "input.h"
#endif
static Portal p;
static int record_presses, record_releases;
static int stage, cancels, presses, releases, result=1;
static GtkWidget *window;
static void event(const char *id, gboolean down, void *user) {
    (void)user;
    if (g_str_equal(id,"cancel") && down) { cancels++; portal_capture_escape(&p,FALSE); }
}
static void record_signal(GDBusConnection *bus, const char *sender, const char *path,
                          const char *interface, const char *signal, GVariant *parameters, void *user) {
    (void)bus; (void)sender; (void)path; (void)interface; (void)signal; (void)user;
    gboolean down;
    g_variant_get(parameters, "(b)", &down);
    if (down) record_presses++; else record_releases++;
    portal_capture_escape(&p, down);
}
static gboolean received(GtkWidget *w, GdkEventKey *e, void *user) {
    (void)w;(void)user;
    if (e->keyval==GDK_KEY_Escape) {
        if(e->type==GDK_KEY_PRESS) presses++; else releases++;
    }
    return FALSE;
}
static gboolean key(guint symbol,gboolean down) {
    GError *error=NULL;
#ifdef KEYSCRIBE_KDE_ESCAPE_TEST
    int code=input_keycode(symbol);
    if(code<0){g_printerr("No keycode for test key\n");gtk_main_quit();return FALSE;}
    symbol=code;
#endif
    GVariant *reply=g_dbus_connection_call_sync(p.bus,"net.gitools.keyscribe.Driver","/net/gitools/keyscribe/Driver","net.gitools.keyscribe.Driver","Key",g_variant_new("(ub)",symbol,down),NULL,0,1000,NULL,&error);
    if(!reply){g_printerr("inject: %s\n",error->message);g_clear_error(&error);gtk_main_quit();return FALSE;}
#ifdef KEYSCRIBE_KDE_ESCAPE_TEST
    gboolean injected=FALSE;
    g_variant_get(reply,"(b)",&injected);
    if(!injected){g_printerr("Private test lost focus; injection stopped\n");gtk_main_quit();}
    g_variant_unref(reply);return injected;
#else
    g_variant_unref(reply);return TRUE;
#endif
}
#define CHECK(x) do {if(!(x)){g_printerr("FAIL stage=%d cancels=%d escape=%d/%d: %s\n",stage,cancels,presses,releases,#x);gtk_main_quit();return G_SOURCE_REMOVE;}}while(0)
static gboolean tick(void *user) {
    (void)user;
    switch(stage) {
    case 0: CHECK(key(GDK_KEY_Shift_L,TRUE));CHECK(key(GDK_KEY_Shift_L,FALSE));break;
    case 1: CHECK(key(GDK_KEY_F12,TRUE));break;
    case 2: CHECK(record_presses==1); CHECK(key(GDK_KEY_Escape,TRUE));break;
    case 3: CHECK(cancels==1);CHECK(key(GDK_KEY_Escape,FALSE));CHECK(key(GDK_KEY_F12,FALSE));break;
    case 4: CHECK(record_releases==1); CHECK(presses==0 && releases==0);CHECK(key(GDK_KEY_Escape,TRUE));CHECK(key(GDK_KEY_Escape,FALSE));break;
    case 5: CHECK(presses==1 && releases==1);portal_capture_escape(&p,TRUE);break;
    case 6: CHECK(key(GDK_KEY_Escape,TRUE));break;
    case 7: CHECK(cancels==2);CHECK(key(GDK_KEY_Escape,FALSE));break;
    case 8: CHECK(presses==1 && releases==1);portal_capture_escape(&p,TRUE);break;
    case 9: CHECK(key(GDK_KEY_Control_L,TRUE));CHECK(key(GDK_KEY_Alt_L,TRUE));break;
    case 10: CHECK(key(GDK_KEY_Escape,TRUE));break;
    case 11: CHECK(cancels==3);CHECK(key(GDK_KEY_Escape,FALSE));break;
    case 12: CHECK(key(GDK_KEY_Alt_L,FALSE));CHECK(key(GDK_KEY_Control_L,FALSE));break;
    default: CHECK(presses==1 && releases==1);result=0;g_print("PASS: real desktop: push-to-talk release and toggle cancellation, modifier Escape, press/release swallowed, idle Escape delivered\n");gtk_main_quit();return G_SOURCE_REMOVE;
    }
    stage++;return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
    gtk_init(&argc,&argv);
    GError *error=NULL;
    if(!portal_init(&p,event,NULL,&error)){g_printerr("init %s\n",error->message);return 1;}
    guint own=g_bus_own_name_on_connection(p.bus,"net.gitools.keyscribe",0,NULL,NULL,NULL,NULL);
    g_dbus_connection_signal_subscribe(p.bus, "net.gitools.keyscribe.Driver",
        "net.gitools.keyscribe.Driver", "Record", "/net/gitools/keyscribe/Driver",
        NULL, 0, record_signal, NULL, NULL);
    window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),"Private Escape test");
    gtk_container_add(GTK_CONTAINER(window),gtk_entry_new());
    g_signal_connect(window,"key-press-event",G_CALLBACK(received),NULL);
    g_signal_connect(window,"key-release-event",G_CALLBACK(received),NULL);
    gtk_widget_show_all(window);gtk_window_present(GTK_WINDOW(window));
    g_timeout_add(500,tick,NULL);gtk_main();
    portal_clear(&p);g_bus_unown_name(own);return result;
}
