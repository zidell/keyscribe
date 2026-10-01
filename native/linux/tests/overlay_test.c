#include "../src/overlay.c"
int main(int argc, char **argv) {
    title = g_strdup("녹음 중 (00:12)");
    recording = TRUE;
    volume = 0.5;
    phase = 0;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 260, 52);
    cairo_t *cr = cairo_create(surface);
    draw(NULL, cr, NULL);
    cairo_surface_flush(surface);
    guchar *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    g_assert_cmpint(data[3], ==, 0);
    g_assert_cmpint(data[26 * stride + 130 * 4 + 3], >, 200);
    for (int i = 0; i < 5; i++) {
        int bar = 26 * stride + (211 + i * 7) * 4;
        g_assert_cmpint(data[bar + 2], >, 200);
        g_assert_cmpint(data[26 * stride + (215 + i * 7) * 4 + 2], <, 60);
    }
    if (argc == 2)
        g_assert_cmpint(cairo_surface_write_to_png(surface, argv[1]), ==, CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_free(title);
    g_print(
        "PASS: original 260x52 widget, transparent rounded corners, original five-bar waveform\n");
    return 0;
}
