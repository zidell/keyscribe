#include "../src/overlay.c"
static void assert_text_centered(cairo_surface_t *surface) {
    cairo_surface_flush(surface);
    guchar *pixels = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    int top = 52, bottom = -1;
    for (int y = 0; y < 52; y++)
        for (int x = 40; x < 205; x++) {
            guchar *pixel = pixels + y * stride + x * 4;
            if (pixel[0] > 200 && pixel[1] > 200 && pixel[2] > 200) {
                top = MIN(top, y);
                bottom = MAX(bottom, y);
            }
        }
    g_assert_cmpint(bottom, >, top);
    g_assert_cmpfloat(fabs((top + bottom + 1) / 2.0 - 26), <=, 1);
}
int main(int argc, char **argv) {
    title = g_strdup("녹음 중 (00:12)");
    recording = TRUE;
    volume = 0.5;
    phase = 0;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 260, 52);
    cairo_t *cr = cairo_create(surface);
    draw(NULL, cr, NULL);
    assert_text_centered(surface);
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
    g_free(title);
    title = g_strdup("변환 중...");
    recording = FALSE;
    draw(NULL, cr, NULL);
    assert_text_centered(surface);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_free(title);
    title = g_strdup("녹음 중 (00:12)");
    recording = TRUE;
    widget_scale = 1.4;
    g_assert_cmpint(widget_pixels(260), ==, 364);
    g_assert_cmpint(widget_pixels(52), ==, 73);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 364, 73);
    cr = cairo_create(surface);
    draw(NULL, cr, NULL);
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);
    g_assert_cmpint(data[3], ==, 0);
    g_assert_cmpint(data[36 * stride + 355 * 4 + 3], >, 200);
    g_assert_cmpint(data[36 * stride + 296 * 4 + 2], >, 200);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_free(title);
    g_print(
        "PASS: original 260x52 widget, transparent rounded corners, original five-bar waveform\n");
    return 0;
}
