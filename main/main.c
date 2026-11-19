// IBM MDA-timing video (720x350 @ ~49.8 Hz, 18.432 kHz hsync) from the
// ESP32-S3 LCD_CAM peripheral, for a TTL monochrome monitor on a DE-9.
//
// Pixel byte: bit0 = video (GPIO4 -> DE-9 pin 7), bit1 = intensity (GPIO5 -> pin 6)
#include <string.h>
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_cache.h"

#define PIN_VIDEO 4
#define PIN_INTEN 5
#define PIN_HSYNC 6
#define PIN_VSYNC 7

#define W 720
#define H 350
#define PX_ON  0x01
#define PX_HI  0x03

static const char *TAG = "mda";
static uint8_t *fb;

static inline void px(int x, int y, uint8_t v)
{
    if ((unsigned)x < W && (unsigned)y < H) fb[y * W + x] = v;
}

static void line(int x0, int y0, int x1, int y1, uint8_t v)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        px(x0, y0, v);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void rect(int x, int y, int w, int h, uint8_t v, bool fill)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (fill || j == y || j == y + h - 1 || i == x || i == x + w - 1) px(i, j, v);
}

static void test_pattern(void)
{
    memset(fb, 0, W * H);
    rect(0, 0, W, H, PX_HI, false);          // outer border: shows overscan/centering
    rect(4, 4, W - 8, H - 8, PX_ON, false);  // inner border, normal intensity
    for (int x = 0; x < W; x += 72) line(x, 0, x, H - 1, PX_ON);   // 10 columns
    for (int y = 0; y < H; y += 35) line(0, y, W - 1, y, PX_ON);   // 10 rows
    line(0, 0, W - 1, H - 1, PX_HI);         // diagonals: check linearity
    line(W - 1, 0, 0, H - 1, PX_HI);
    // intensity ramp: left half normal, right half bright
    rect(100, 140, 120, 70, PX_ON, true);
    rect(500, 140, 120, 70, PX_HI, true);
    // 1-pixel checkerboard: highest-frequency pixel pattern, checks video bandwidth
    for (int y = 230; y < 330; y++)
        for (int x = 300; x < 420; x++) px(x, y, ((x ^ y) & 1) ? PX_ON : 0);
    // circle
    for (int a = 0; a < 3600; a++) {
        float t = a * 0.0017453293f;
        px(360 + (int)(60 * __builtin_cosf(t)), 120 + (int)(60 * __builtin_sinf(t) * 0.8f), PX_HI);
    }
    esp_cache_msync(fb, W * H, ESP_CACHE_MSYNC_FLAG_DIR_C2M);  // may be a no-op for internal RAM
}

void app_main(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 8,
        .bits_per_pixel = 8,
        .num_fbs = 1,
        .dma_burst_size = 64,
        .hsync_gpio_num = PIN_HSYNC,
        .vsync_gpio_num = PIN_VSYNC,
        .de_gpio_num = -1,                 // HV mode: no data-enable
        .pclk_gpio_num = -1,               // monitor has no pixel clock input
        .disp_gpio_num = -1,
        .data_gpio_nums = { PIN_VIDEO, PIN_INTEN, -1, -1, -1, -1, -1, -1,
                            -1, -1, -1, -1, -1, -1, -1, -1 },
        .timings = {
            .pclk_hz = 16257000,
            .h_res = W,
            .v_res = H,
            .hsync_front_porch = 18,
            .hsync_pulse_width = 108,
            .hsync_back_porch = 36,
            .vsync_front_porch = 1,
            .vsync_pulse_width = 16,
            .vsync_back_porch = 3,
            .flags = {
                .hsync_idle_low = true,    // active-high HSYNC
                .vsync_idle_low = false,   // active-low VSYNC (MDA)
                .de_idle_high = false,
                .pclk_active_neg = false,
            },
        },
        // Framebuffer MUST stay in internal RAM (252 KB). With the PSRAM framebuffer + bounce
        // buffer the picture jittered/bounced vertically on this monitor.
        .flags = { .fb_in_psram = false },
    };

    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, (void **)&fb));
    test_pattern();
    ESP_LOGI(TAG, "MDA video running: %dx%d, pclk %d Hz", W, H, 16257000);
}
