// IBM MDA-timing video (720x350 @ ~49.8 Hz, 18.432 kHz hsync) from the
// ESP32-S3 LCD_CAM peripheral, for a TTL monochrome monitor on a DE-9.
//
// Pixel byte: bit0 = video (GPIO4 -> DE-9 pin 7), bit1 = intensity (GPIO5 -> pin 6)
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "knot.h"

#define PIN_VIDEO 4
#define PIN_INTEN 5
#define PIN_HSYNC 6
#define PIN_VSYNC 7

#define W 720
#define H 350
#define PX_ON  0x01
#define PX_HI  0x03

// What to show:
//   0  static calibration pattern
//   1  bouncing wireframe icosahedron (frame buffer in internal RAM)
//   2  shaded 3D torus knot (two frame buffers in PSRAM behind a bounce buffer)
#ifndef MODE
#define MODE 2
#endif

// In mode 2 the RGB driver fills two small internal-RAM bounce buffers from the PSRAM frame buffer.
// The number of bounce chunks per frame must be even: with an odd number (10 lines = 35 chunks,
// 70 lines = 5 chunks) the two bounce buffers fall out of step with the frame and the picture
// repeats or swaps bands of lines. 35 lines gives 10 chunks.
#define BOUNCE_LINES 35
_Static_assert((350 / BOUNCE_LINES) % 2 == 0 && 350 % BOUNCE_LINES == 0, "need an even number of bounce chunks");

// Pixels are taller than wide on a 4:3 tube showing 720x350. Scale x by this to keep shapes round.
#define ASPECT_X 1.54f

static const char *TAG = "mda";
static uint8_t *fb;    // scanned out by DMA, internal RAM
static uint8_t *dst;   // buffer that px/line/rect draw into

static inline void px(int x, int y, uint8_t v)
{
    if ((unsigned)x < W && (unsigned)y < H) dst[y * W + x] = v;
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

__attribute__((unused)) static void test_pattern(void)
{
    dst = fb;
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

// ---- 3D screensaver: icosahedron, depth-cued, bouncing -------------------------------

static SemaphoreHandle_t vsync_sem;

static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_data_t *e, void *u)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(vsync_sem, &woken);
    return woken == pdTRUE;
}

#define NV 12
#define MAX_EDGES 30

__attribute__((unused)) static void screensaver(esp_lcd_panel_handle_t panel)
{
    (void)panel;
    uint8_t *back = heap_caps_malloc(W * H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(back ? ESP_OK : ESP_ERR_NO_MEM);

    // Icosahedron: (0,+-1,+-phi) and its cyclic permutations. Edges join vertices 2 apart.
    const float phi = 1.6180339887f, n = 1.0f / sqrtf(1.0f + phi * phi);
    float v[NV][3];
    int k = 0;
    for (int a = -1; a <= 1; a += 2)
        for (int b = -1; b <= 1; b += 2) {
            v[k][0] = 0;       v[k][1] = a * n;       v[k][2] = b * phi * n; k++;
            v[k][0] = a * n;   v[k][1] = b * phi * n; v[k][2] = 0;           k++;
            v[k][0] = b * phi * n; v[k][1] = 0;       v[k][2] = a * n;       k++;
        }
    int edge[MAX_EDGES][2], ne = 0;
    for (int i = 0; i < NV; i++)
        for (int j = i + 1; j < NV; j++) {
            float dx = v[i][0] - v[j][0], dy = v[i][1] - v[j][1], dz = v[i][2] - v[j][2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 3.9f * n * n && d2 < 4.1f * n * n && ne < MAX_EDGES) { edge[ne][0] = i; edge[ne][1] = j; ne++; }
        }

    const float R = 95.0f;                       // vertical radius in pixels
    float cx = 300, cy = 150, vx = 1.7f, vy = 1.1f;
    float ax = 0, ay = 0, az = 0;
    int sx[NV], sy[NV];
    float zz[NV];

    for (;;) {
        // rotate and project
        float cax = cosf(ax), sax = sinf(ax), cay = cosf(ay), say = sinf(ay), caz = cosf(az), saz = sinf(az);
        for (int i = 0; i < NV; i++) {
            float x = v[i][0], y = v[i][1], z = v[i][2], t;
            t = y * cax - z * sax; z = y * sax + z * cax; y = t;       // about X
            t = x * cay + z * say; z = -x * say + z * cay; x = t;       // about Y
            t = x * caz - y * saz; y = x * saz + y * caz; x = t;       // about Z
            float persp = 1.0f / (1.0f - 0.22f * z);                    // +z is toward the viewer
            sx[i] = (int)(cx + x * R * ASPECT_X * persp);
            sy[i] = (int)(cy + y * R * persp);
            zz[i] = z;
        }
        dst = back;
        memset(back, 0, W * H);
        for (int e = 0; e < ne; e++) {
            int a = edge[e][0], b = edge[e][1];
            line(sx[a], sy[a], sx[b], sy[b], (zz[a] + zz[b]) > 0 ? PX_HI : PX_ON);
        }

        // wait for the start of vertical blanking, then copy faster than the beam
        xSemaphoreTake(vsync_sem, 0);
        xSemaphoreTake(vsync_sem, portMAX_DELAY);
        memcpy(fb, back, W * H);

        ax += 0.013f; ay += 0.021f; az += 0.007f;
        cx += vx; cy += vy;
        float hx = R * ASPECT_X * 1.25f, hy = R * 1.25f;
        if (cx < hx + 6)     { cx = hx + 6;     vx = -vx; }
        if (cx > W - hx - 6) { cx = W - hx - 6; vx = -vx; }
        if (cy < hy + 6)     { cy = hy + 6;     vy = -vy; }
        if (cy > H - hy - 6) { cy = H - hy - 6; vy = -vy; }
    }
}

void app_main(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 8,
        .bits_per_pixel = 8,
        .num_fbs = MODE == 2 ? 2 : 1,
        .dma_burst_size = 64,
        .bounce_buffer_size_px = MODE == 2 ? BOUNCE_LINES * W : 0,
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
        .flags = { .fb_in_psram = MODE == 2 },
    };

    esp_lcd_panel_handle_t panel;
    vsync_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_vsync = on_vsync };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, NULL));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    ESP_LOGI(TAG, "MDA video running: %dx%d, pclk %d Hz, mode %d", W, H, 16257000, MODE);
#if MODE == 2
    uint8_t *f0, *f1;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, (void **)&f0, (void **)&f1));
    knot_run(panel, f0, f1, vsync_sem);
#else
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, (void **)&fb));
#if MODE == 1
    screensaver(panel);
#else
    test_pattern();
#endif
#endif
}
