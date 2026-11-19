// Software 3D: a lit torus knot (1,536 triangles) with Gouraud shading, painter's-algorithm depth
// sorting, back-face culling and an ordered dither down to the monitor's three levels
// (black, normal, bright). The two cores each rasterise one half of the screen.
#include "knot.h"
#include "frames.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define W 720
#define H 350
#define U_SEG 64
#define V_SEG 12
#define NVERT (U_SEG * V_SEG)
#define NTRI  (NVERT * 2)
#define NSTAR 140
#define ASPECT_X 1.54f      // pixels are taller than wide on a 4:3 tube
#define KEYS 1024

static const char *TAG = "knot";

typedef struct { int16_t x, y; uint8_t lum; int8_t nz; } sv_t;

static float bp[NVERT][3], bn[NVERT][3];       // model-space position and normal
static sv_t sv[NVERT];                         // projected, lit vertices
static float vzf[NVERT];                       // view-space z per vertex
static uint16_t tri[NTRI][3];
static uint16_t order[NTRI];                   // visible triangles, far to near
static volatile int norder;
static uint8_t bayer[64];
static int32_t recip[W + 2];                   // 65536 / n, replaces a divide per scanline
static const uint8_t lut[4] = { 0x00, 0x01, 0x03, 0x03 };   // black, normal, bright (4th entry unused)

typedef struct { int16_t x, y; int8_t vx; } star_t;
static star_t star[NSTAR];

static uint8_t *back;                          // buffer being drawn this frame
static TaskHandle_t main_task, worker_task;

// ---- scene setup ---------------------------------------------------------------------

static void knot_point(float t, float *p)
{
    float r = 2.0f + cosf(3.0f * t);
    p[0] = r * cosf(2.0f * t);
    p[1] = r * sinf(2.0f * t);
    p[2] = sinf(3.0f * t);
}

static void norm3(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void build_scene(void)
{
    const float rad = 0.38f, dt = 0.0008f;
    for (int u = 0; u < U_SEG; u++) {
        float t = 2.0f * (float)M_PI * u / U_SEG;
        float p0[3], p1[3], p2[3], T[3], A[3], N[3], B[3];
        knot_point(t - dt, p0); knot_point(t, p1); knot_point(t + dt, p2);
        for (int i = 0; i < 3; i++) { T[i] = p2[i] - p0[i]; A[i] = p2[i] + p0[i] - 2 * p1[i]; }
        norm3(T);
        float d = A[0] * T[0] + A[1] * T[1] + A[2] * T[2];
        for (int i = 0; i < 3; i++) N[i] = A[i] - d * T[i];
        norm3(N);
        B[0] = T[1] * N[2] - T[2] * N[1];
        B[1] = T[2] * N[0] - T[0] * N[2];
        B[2] = T[0] * N[1] - T[1] * N[0];
        for (int v = 0; v < V_SEG; v++) {
            float ph = 2.0f * (float)M_PI * v / V_SEG, c = cosf(ph), s = sinf(ph);
            int k = u * V_SEG + v;
            for (int i = 0; i < 3; i++) {
                bn[k][i] = c * N[i] + s * B[i];
                bp[k][i] = p1[i] + rad * bn[k][i];
            }
        }
    }
    for (int u = 0; u < U_SEG; u++)
        for (int v = 0; v < V_SEG; v++) {
            int u1 = (u + 1) % U_SEG, v1 = (v + 1) % V_SEG;
            uint16_t a = u * V_SEG + v, b = u1 * V_SEG + v, c = u1 * V_SEG + v1, d = u * V_SEG + v1;
            int t = 2 * (u * V_SEG + v);
            tri[t][0] = a; tri[t][1] = b; tri[t][2] = c;
            tri[t + 1][0] = a; tri[t + 1][1] = c; tri[t + 1][2] = d;
        }
    for (int i = 1; i < W + 2; i++) recip[i] = 65536 / i;
    // 8x8 Bayer matrix by recursion, scaled to 0..255
    uint8_t m[8][8] = { { 0 } };
    for (int size = 1; size < 8; size *= 2)
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                int v = m[y][x] * 4;
                m[y][x] = v; m[y][x + size] = v + 2; m[y + size][x] = v + 3; m[y + size][x + size] = v + 1;
            }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) bayer[y * 8 + x] = m[y][x] * 4 + 2;
    uint32_t seed = 12345;
    for (int i = 0; i < NSTAR; i++) {
        seed = seed * 1664525u + 1013904223u; star[i].x = (seed >> 8) % W;
        seed = seed * 1664525u + 1013904223u; star[i].y = (seed >> 8) % H;
        seed = seed * 1664525u + 1013904223u; star[i].vx = 1 + (seed >> 12) % 3;
    }
}

// ---- per-frame transform, lighting, culling and sorting (core 0) -------------------------

static void prepare(float time)
{
    float ax = time * 0.55f, ay = time * 0.83f, az = time * 0.31f;
    float cx_ = cosf(ax), sx_ = sinf(ax), cy_ = cosf(ay), sy_ = sinf(ay), cz_ = cosf(az), sz_ = sinf(az);
    // R = Rz * Ry * Rx
    const float m00 = cz_ * cy_, m01 = cz_ * sy_ * sx_ - sz_ * cx_, m02 = cz_ * sy_ * cx_ + sz_ * sx_;
    const float m10 = sz_ * cy_, m11 = sz_ * sy_ * sx_ + cz_ * cx_, m12 = sz_ * sy_ * cx_ - cz_ * sx_;
    const float m20 = -sy_,      m21 = cy_ * sx_,                   m22 = cy_ * cx_;
    float S = 46.0f + 4.0f * sinf(time * 0.7f);
    const float D = 10.0f, cx = W / 2, cy = H / 2;

    float L[3] = { cosf(time * 1.3f), 0.55f * sinf(time * 0.9f) + 0.25f, 0.85f };
    norm3(L);
    float Hh[3] = { L[0], L[1], L[2] + 1.0f };
    norm3(Hh);

    for (int i = 0; i < NVERT; i++) {
        const float px = bp[i][0], py = bp[i][1], pz = bp[i][2];
        const float qx = bn[i][0], qy = bn[i][1], qz = bn[i][2];
        float x = m00 * px + m01 * py + m02 * pz;
        float y = m10 * px + m11 * py + m12 * pz;
        float z = m20 * px + m21 * py + m22 * pz;
        float nx = m00 * qx + m01 * qy + m02 * qz;
        float ny = m10 * qx + m11 * qy + m12 * qz;
        float nz = m20 * qx + m21 * qy + m22 * qz;

        float f = D / (D - z);
        sv[i].x = (int16_t)(cx + x * S * ASPECT_X * f);
        sv[i].y = (int16_t)(cy + y * S * f);
        vzf[i] = z;
        sv[i].nz = (int8_t)(nz * 127.0f);

        float ndl = nx * L[0] + ny * L[1] + nz * L[2];
        float diff = ndl > 0 ? ndl : 0;
        float ndh = nx * Hh[0] + ny * Hh[1] + nz * Hh[2];
        if (ndh < 0) ndh = 0;
        float sp = ndh * ndh; sp *= sp; sp *= sp; sp *= sp;            // ^16
        float lum = 0.10f + 0.62f * diff + 0.60f * sp;
        lum *= 0.62f + 0.38f * ((z + 3.5f) / 7.0f);                    // far parts dimmer
        int l = (int)(lum * 255.0f);
        sv[i].lum = l < 2 ? 2 : (l > 253 ? 253 : l);     // keep interpolated values inside 0..255 so the pixel loop needs no clamps
    }

    // cull back faces, bucket-sort the rest by average depth, far first
    static uint16_t key[NTRI], count[KEYS + 1];
    memset(count, 0, sizeof(count));
    int n = 0;
    for (int t = 0; t < NTRI; t++) {
        int a = tri[t][0], b = tri[t][1], c = tri[t][2];
        if (sv[a].nz + sv[b].nz + sv[c].nz < -20) { key[t] = 0xFFFF; continue; }
        float z = (vzf[a] + vzf[b] + vzf[c]) * (1.0f / 3.0f);
        int k = (int)((z + 3.7f) * (KEYS - 1) / 7.4f);
        key[t] = k < 0 ? 0 : (k > KEYS - 1 ? KEYS - 1 : k);
        count[key[t] + 1]++;
        n++;
    }
    for (int i = 0; i < KEYS; i++) count[i + 1] += count[i];
    for (int t = 0; t < NTRI; t++)
        if (key[t] != 0xFFFF) order[count[key[t]]++] = t;
    norder = n;

    for (int i = 0; i < NSTAR; i++) {
        star[i].x -= star[i].vx;
        if (star[i].x < 0) star[i].x += W;
    }
}

// ---- rasteriser (runs on both cores, each over its own band of rows) ---------------------

static IRAM_ATTR void fill_tri(uint8_t *fb, int yb0, int yb1, const sv_t *p0, const sv_t *p1, const sv_t *p2)
{
    const sv_t *t;
    if (p0->y > p1->y) { t = p0; p0 = p1; p1 = t; }
    if (p1->y > p2->y) { t = p1; p1 = p2; p2 = t; }
    if (p0->y > p1->y) { t = p0; p0 = p1; p1 = t; }
    int ya = p0->y, ym = p1->y, yc = p2->y;
    if (yc <= ya || yc <= yb0 || ya >= yb1) return;

    int dl = yc - ya;
    int32_t sxl = ((p2->x - p0->x) << 16) / dl;
    int32_t sll = ((p2->lum - p0->lum) << 16) / dl;
    int32_t sx1 = 0, sl1 = 0, sx2 = 0, sl2 = 0;
    if (ym > ya) { sx1 = ((p1->x - p0->x) << 16) / (ym - ya); sl1 = ((p1->lum - p0->lum) << 16) / (ym - ya); }
    if (yc > ym) { sx2 = ((p2->x - p1->x) << 16) / (yc - ym); sl2 = ((p2->lum - p1->lum) << 16) / (yc - ym); }

    int ys = ya > yb0 ? ya : yb0, ye = yc < yb1 ? yc : yb1;
    for (int y = ys; y < ye; y++) {
        int32_t xa = (p0->x << 16) + (y - ya) * sxl, la = (p0->lum << 16) + (y - ya) * sll, xb, lb;
        if (y < ym) { xb = (p0->x << 16) + (y - ya) * sx1; lb = (p0->lum << 16) + (y - ya) * sl1; }
        else        { xb = (p1->x << 16) + (y - ym) * sx2; lb = (p1->lum << 16) + (y - ym) * sl2; }
        if (xa > xb) { int32_t s = xa; xa = xb; xb = s; s = la; la = lb; lb = s; }
        int x0 = (xa + 0x8000) >> 16, x1 = (xb + 0x8000) >> 16;
        if (x1 <= x0) continue;
        int32_t step = (int32_t)(((int64_t)(lb - la) * recip[x1 - x0]) >> 16), l = la;
        if (x0 < 0) { l += step * -x0; x0 = 0; }
        if (x1 > W) x1 = W;
        const uint8_t *br = &bayer[(y & 7) * 8];
        uint8_t *row = fb + y * W;
        for (int x = x0; x < x1; x++, l += step) {
            int s = l >> 15;                         // 0..510
            row[x] = lut[(s >> 8) + ((s & 255) > br[x & 7])];
        }
    }
}

#define STRIP 35
static uint8_t strip_buf[2][STRIP * W];        // one per core, internal RAM

static int next_strip;

// Both cores call this: each repeatedly takes the next unrendered strip, renders it into its own
// internal-RAM buffer and copies it into the frame buffer. A core that is busier with interrupts
// simply takes fewer strips.
static IRAM_ATTR void raster_strips(uint8_t *fb, int core)
{
    uint8_t *buf = strip_buf[core];
    int s;
    while ((s = __atomic_fetch_add(&next_strip, 1, __ATOMIC_RELAXED)) < H / STRIP) {
        int ys = s * STRIP, ye = ys + STRIP;
        uint8_t *base = buf - ys * W;              // so that base + y * W addresses row y of the strip
        memset(buf, 0, STRIP * W);
        for (int i = 0; i < NSTAR; i++)
            if (star[i].y >= ys && star[i].y < ye) base[star[i].y * W + star[i].x] = 0x03;
        int n = norder;
        for (int i = 0; i < n; i++) {
            const uint16_t *v = tri[order[i]];
            fill_tri(base, ys, ye, &sv[v[0]], &sv[v[1]], &sv[v[2]]);
        }
        memcpy(fb + ys * W, buf, STRIP * W);
    }
}

static void worker(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        raster_strips(back, 1);
        xTaskNotifyGive(main_task);
    }
}

// ---- main loop -------------------------------------------------------------------------

void knot_run(esp_lcd_panel_handle_t panel, uint8_t *fb0, uint8_t *fb1)
{
    build_scene();
    main_task = xTaskGetCurrentTaskHandle();
    xTaskCreatePinnedToCore(worker, "raster1", 4096, NULL, 5, &worker_task, 1);

    uint8_t *front = fb0;
    back = fb1;
    memset(fb0, 0, W * H);
    memset(fb1, 0, W * H);
    int64_t t_start = esp_timer_get_time(), t_acc = 0, t_report = t_start, p_acc = 0, r_acc = 0, w_acc = 0;
    int frames = 0;

    for (;;) {
        int64_t t0 = esp_timer_get_time();
        prepare((float)((t0 - t_start) * 1e-6));
        int64_t t1 = esp_timer_get_time();
        __atomic_store_n(&next_strip, 0, __ATOMIC_SEQ_CST);
        xTaskNotifyGive(worker_task);
        raster_strips(back, 0);
        int64_t t2 = esp_timer_get_time();
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t t3 = esp_timer_get_time();
        t_acc += t3 - t0; p_acc += t1 - t0; r_acc += t2 - t1; w_acc += t3 - t2;

        // show the finished buffer, then wait until the driver has latched it, which frees the old one
        uint32_t since = g_latched;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, W, H, back));
        wait_latched(since);
        uint8_t *t = front; front = back; back = t;

        if (++frames == 100) {
            int64_t now = esp_timer_get_time();
            ESP_LOGI(TAG, "%.1f fps, %.1f ms render per frame, %d of %d triangles drawn",
                     100e6 / (double)(now - t_report), t_acc / 100000.0, norder, NTRI);
            ESP_LOGI(TAG, "per frame: prepare %.1f ms, core0 raster %.1f ms, wait for core1 %.1f ms",
                     p_acc / 100000.0, r_acc / 100000.0, w_acc / 100000.0);
            t_acc = p_acc = r_acc = w_acc = 0; frames = 0; t_report = now;
        }
    }
}
