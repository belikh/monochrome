// Network frame buffer: joins Wi-Fi, listens on a TCP port and a UDP port (same number) and shows
// the frames a client sends.
// While no client is connected it shows the device's IP address and port on the screen.
#include "netfb.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "frames.h"
#include "miniz.h"          // ROM deflate decompressor (tinfl)

#define W 720
#define H 350
#define FRAME_PIXELS (W * H)
#define FMT_8BPP 0   // 252,000 bytes, one byte per pixel (bit 0 video, bit 1 intensity)
#define FMT_1BPP 1   //  31,500 bytes, 1 bit per pixel, MSB first: 0 black, 1 normal
#define FMT_2BPP 2   //  63,000 bytes, 2 bits per pixel, MSB first: 0 black, 1 normal, 2 or 3 bright
#define RX_MAX 63000        // largest packed (uncompressed) payload: 2 bits per pixel
#define Z_MAX  65536        // largest compressed TCP frame we accept (kept in internal RAM)
#define FLAG_LAST 1         // UDP: last packet of a frame
#define FLAG_ZLIB 2         // UDP: payload is zlib-compressed rows

static const char *TAG = "netfb";

static esp_lcd_panel_handle_t g_panel;
static uint8_t *g_front, *g_back;
static SemaphoreHandle_t g_lock;     // one source composes and shows a frame at a time
static uint8_t rawbuf[RX_MAX];      // packed rows after inflating; used under g_lock
static uint8_t zbuf[Z_MAX];         // compressed TCP frame
static tinfl_decompressor inflator; // about 11 KB, kept off the task stacks
static bool retiring;                // g_back was on screen until the last submit()
static uint32_t submit_latch;

static char ip_str[16] = "0.0.0.0";
static volatile bool got_ip;

// ---- Wi-Fi ---------------------------------------------------------------------------------

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        got_ip = false;
        strcpy(ip_str, "0.0.0.0");
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&e->ip_info.ip));
        got_ip = true;
        ESP_LOGI(TAG, "got IP %s, listening on port %d", ip_str, CONFIG_MONO_PORT);
    }
}

static void wifi_start(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, CONFIG_MONO_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_MONO_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = CONFIG_MONO_WIFI_PASSWORD[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    if (!CONFIG_MONO_WIFI_SSID[0]) ESP_LOGE(TAG, "no Wi-Fi SSID set: run idf.py menuconfig, Monochrome menu");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);                 // power save adds latency and costs throughput
}

// ---- status screen: IP address and port in a 5x7 digit font ---------------------------------

static const uint8_t glyph[12][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},              // '.'
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},              // ':'
};

// Pixels are taller than wide on a 4:3 tube, so glyphs are scaled 7 wide by 5 tall.
static void draw_text(uint8_t *fb, const char *s, int x, int y, uint8_t v)
{
    for (; *s; s++, x += 42) {
        int g = *s == '.' ? 10 : *s == ':' ? 11 : (*s >= '0' && *s <= '9') ? *s - '0' : -1;
        if (g < 0) continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (glyph[g][r] & (0x10 >> c))
                    for (int dy = 0; dy < 5; dy++)
                        memset(fb + (y + r * 5 + dy) * W + x + c * 7, v, 7);
    }
}

static void draw_status(uint8_t *fb)
{
    memset(fb, 0, FRAME_PIXELS);
    uint8_t edge = got_ip ? 0x03 : 0x01;
    for (int x = 0; x < W; x++) { fb[x] = edge; fb[(H - 1) * W + x] = edge; }
    for (int y = 0; y < H; y++) { fb[y * W] = edge; fb[y * W + W - 1] = edge; }
    char port[8];
    snprintf(port, sizeof(port), ":%d", CONFIG_MONO_PORT);
    int w = (int)strlen(ip_str) * 42;
    draw_text(fb, ip_str, (W - w) / 2, 120, 0x03);
    draw_text(fb, port, (W - (int)strlen(port) * 42) / 2, 190, 0x01);
}

// ---- frame handling ------------------------------------------------------------------------

// Shows g_back. It does not wait: the old front buffer (the new g_back) is only reused after the
// driver has latched the new one, which begin_write() waits for, usually without blocking.
static void submit(void)
{
    submit_latch = g_latched;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(g_panel, 0, 0, W, H, g_back));
    uint8_t *t = g_front; g_front = g_back; g_back = t;
    retiring = true;
}

// Call before writing into g_back.
static void begin_write(void)
{
    if (retiring) { wait_latched(submit_latch); retiring = false; }
}

static bool recv_all(int s, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n) {
        int r = recv(s, p, n, 0);
        if (r <= 0) return false;
        p += r; n -= r;
    }
    return true;
}

static uint32_t exp1[256][2];   // one packed byte -> 8 pixel bytes
static uint32_t exp2[256];      // one packed byte -> 4 pixel bytes

static void build_tables(void)
{
    for (int b = 0; b < 256; b++) {
        uint8_t px[8];
        for (int i = 0; i < 8; i++) px[i] = (b >> (7 - i)) & 1 ? 0x01 : 0x00;
        memcpy(&exp1[b][0], px, 4);
        memcpy(&exp1[b][1], px + 4, 4);
        static const uint8_t m2[4] = { 0x00, 0x01, 0x03, 0x03 };
        uint8_t q[4];
        for (int i = 0; i < 4; i++) q[i] = m2[(b >> (6 - 2 * i)) & 3];
        memcpy(&exp2[b], q, 4);
    }
}

// Inflates a zlib stream into out. Returns the decompressed length, or -1 on error.
static int inflate_zlib(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    tinfl_init(&inflator);
    size_t isz = in_len, osz = out_cap;
    tinfl_status st = tinfl_decompress(&inflator, in, &isz, out, out, &osz,
                                       TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (st != TINFL_STATUS_DONE) ESP_LOGW(TAG, "inflate status %d after %u input bytes, %u output bytes", (int)st, (unsigned)isz, (unsigned)osz);
    return st == TINFL_STATUS_DONE ? (int)osz : -1;
}

// Expands packed rows into the hidden frame buffer. Returns false if the length does not match.
static bool apply_rows(int fmt, int row0, int nrows, const uint8_t *p, int len)
{
    if (nrows <= 0 || row0 < 0 || row0 + nrows > H) return false;
    uint32_t *out = (uint32_t *)(g_back + row0 * W);
    int px = nrows * W;
    switch (fmt) {
    case FMT_8BPP:
        if (len != px) return false;
        for (int i = 0; i < px / 4; i++) { uint32_t v; memcpy(&v, p + 4 * i, 4); out[i] = v & 0x03030303u; }
        return true;                               // only bits 0 and 1 are wired
    case FMT_1BPP:
        if (len != px / 8) return false;
        for (int i = 0; i < px / 8; i++) { out[2 * i] = exp1[p[i]][0]; out[2 * i + 1] = exp1[p[i]][1]; }
        return true;
    case FMT_2BPP:
        if (len != px / 4) return false;
        for (int i = 0; i < px / 4; i++) out[i] = exp2[p[i]];
        return true;
    default:
        return false;
    }
}

static int payload_len(int fmt)
{
    return fmt == FMT_8BPP ? FRAME_PIXELS : fmt == FMT_1BPP ? FRAME_PIXELS / 8 : fmt == FMT_2BPP ? FRAME_PIXELS / 4 : -1;
}

// TCP: reads and shows one whole frame. Returns false when the connection fails or sends bad data.
// Header: 'M' 'O' 'N' 'O' version(1) format flags reserved. Flags bit 0: the payload is
// zlib-compressed and is preceded by its length as a u32 (little endian).
static bool read_frame(int s)
{
    uint8_t hdr[8];
    if (!recv_all(s, hdr, sizeof(hdr))) return false;            // may wait for the client; no lock held
    if (memcmp(hdr, "MONO", 4) != 0 || hdr[4] != 1) { ESP_LOGW(TAG, "bad frame header"); return false; }
    int fmt = hdr[5], len = payload_len(fmt);
    if (len < 0) { ESP_LOGW(TAG, "unknown frame format %d", fmt); return false; }
    uint32_t zlen = 0;
    if (hdr[6] & 1) {
        uint8_t l4[4];
        if (!recv_all(s, l4, 4)) return false;
        zlen = l4[0] | l4[1] << 8 | l4[2] << 16 | (uint32_t)l4[3] << 24;
        if (zlen == 0 || zlen > Z_MAX) { ESP_LOGW(TAG, "bad compressed length %lu", (unsigned long)zlen); return false; }
    }

    xSemaphoreTake(g_lock, portMAX_DELAY);
    begin_write();
    bool ok;
    if (zlen) {
        ok = recv_all(s, zbuf, zlen);
        if (!ok) {
            ESP_LOGI(TAG, "client closed the connection in the middle of a frame");
        } else {
            uint8_t *dest = fmt == FMT_8BPP ? g_back : rawbuf;
            int got = inflate_zlib(zbuf, zlen, dest, fmt == FMT_8BPP ? FRAME_PIXELS : RX_MAX);
            ok = got == len;
            if (!ok) ESP_LOGW(TAG, "compressed frame: inflated to %d bytes, expected %d (format %d, %lu compressed)", got, len, fmt, (unsigned long)zlen);
            else if (fmt == FMT_8BPP) { uint32_t *o = (uint32_t *)g_back; for (int i = 0; i < FRAME_PIXELS / 4; i++) o[i] &= 0x03030303u; }
            else ok = apply_rows(fmt, 0, H, rawbuf, len);
        }
    } else if (fmt == FMT_8BPP) {
        ok = recv_all(s, g_back, len);                           // straight into the hidden buffer
        if (ok) { uint32_t *o = (uint32_t *)g_back; for (int i = 0; i < FRAME_PIXELS / 4; i++) o[i] &= 0x03030303u; }
    } else {
        ok = recv_all(s, rawbuf, len) && apply_rows(fmt, 0, H, rawbuf, len);
    }
    if (ok) submit();
    xSemaphoreGive(g_lock);
    return ok;
}

// ---- UDP: each datagram carries some rows of a frame ------------------------------------------
// bytes: 'M' 'U' version(1) format seq(u16 LE) first_row(u16 LE) row_count(u8) flags(u8), then rows.
// flags bit 0 marks the last packet of a frame; bit 1 says the rows are zlib-compressed. A frame also ends when the sequence number changes
// or when 50 ms pass with no packets. Rows that never arrive keep the previous frame's pixels.

static bool got_row[H];

// Finishes the frame being composed: rows that never arrived keep what the last shown frame had.
static void finish_udp_frame(void)
{
    for (int y = 0; y < H; y++)
        if (!got_row[y]) memcpy(g_back + y * W, g_front + y * W, W);
    submit();
}

static void udp_task(void *arg)
{
    static uint8_t pkt[1600];
    int us = -1;
    bool pending = false;
    uint16_t cur_seq = 0;
    uint32_t pkts = 0, frames = 0, bad = 0;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        if (!got_ip) {
            if (us >= 0) { close(us); us = -1; }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (us < 0) {
            us = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
            struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(CONFIG_MONO_PORT), .sin_addr.s_addr = htonl(INADDR_ANY) };
            int rcvbuf = 32768;
            setsockopt(us, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
            struct timeval tv = { .tv_usec = 50000 };
            setsockopt(us, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            if (bind(us, (struct sockaddr *)&a, sizeof(a))) {
                ESP_LOGE(TAG, "UDP bind failed"); close(us); us = -1; vTaskDelay(pdMS_TO_TICKS(1000)); continue;
            }
        }
        int n = recv(us, pkt, sizeof(pkt), 0);
        if (n < 0) {                                           // timeout: the frame is finished
            if (pending && xSemaphoreTake(g_lock, 0) == pdTRUE) {
                finish_udp_frame(); frames++; pending = false; xSemaphoreGive(g_lock);
            }
            continue;
        }
        if (n < 10 || pkt[0] != 'M' || pkt[1] != 'U' || pkt[2] != 1) continue;
        uint16_t seq = pkt[4] | pkt[5] << 8, row0 = pkt[6] | pkt[7] << 8;
        int fmt = pkt[3], nrows = pkt[8], flags = pkt[9];
        if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(5)) != pdTRUE) continue;   // a TCP client is busy: drop
        if (pending && seq != cur_seq) { finish_udp_frame(); frames++; pending = false; }
        if (!pending) {
            begin_write();                                     // normally returns at once
            memset(got_row, 0, sizeof(got_row));
            cur_seq = seq; pending = true;
        }
        const uint8_t *body = pkt + 10;
        int blen = n - 10;
        if (flags & FLAG_ZLIB) {                                   // inflate the rows into rawbuf first
            blen = inflate_zlib(body, blen, rawbuf, RX_MAX);
            body = rawbuf;
        }
        if (blen >= 0 && apply_rows(fmt, row0, nrows, body, blen)) {
            memset(&got_row[row0], 1, nrows);
            pkts++;
        } else {
            bad++;
        }
        if (flags & FLAG_LAST) { finish_udp_frame(); frames++; pending = false; }
        xSemaphoreGive(g_lock);
        if (frames >= 100) {
            int64_t now = esp_timer_get_time();
            ESP_LOGI(TAG, "UDP %.1f frames/s, %lu packets per frame, %lu bad", frames * 1e6 / (double)(now - t0),
                     (unsigned long)(pkts / frames), (unsigned long)bad);
            t0 = now; pkts = 0; frames = 0; bad = 0;
        }
    }
}

static void net_task(void *arg)
{
    build_tables();
    xTaskCreatePinnedToCore(udp_task, "udp", 6144, NULL, 5, NULL, 1);
    wifi_start();

    char shown[16] = "";
    bool shown_up = false;
    int ls = -1;
    for (;;) {
        // wait for an address and show it; (re)open the listening socket once we have one
        if (!got_ip) {
            if (ls >= 0) { close(ls); ls = -1; }
            if (strcmp(shown, ip_str) || shown_up) {
                xSemaphoreTake(g_lock, portMAX_DELAY); begin_write(); draw_status(g_back); submit(); xSemaphoreGive(g_lock);
                strlcpy(shown, ip_str, sizeof(shown)); shown_up = false;
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (ls < 0) {
            ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(CONFIG_MONO_PORT), .sin_addr.s_addr = htonl(INADDR_ANY) };
            int one = 1;
            setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            struct timeval tv = { .tv_sec = 1 };
            setsockopt(ls, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));    // lets accept() time out
            if (bind(ls, (struct sockaddr *)&a, sizeof(a)) || listen(ls, 1)) {
                ESP_LOGE(TAG, "bind/listen failed");
                close(ls); ls = -1; vTaskDelay(pdMS_TO_TICKS(1000)); continue;
            }
        }
        if (strcmp(shown, ip_str) || !shown_up) {
            xSemaphoreTake(g_lock, portMAX_DELAY); begin_write(); draw_status(g_back); submit(); xSemaphoreGive(g_lock);
            strlcpy(shown, ip_str, sizeof(shown)); shown_up = true;
        }
        int c = accept(ls, NULL, NULL);
        if (c < 0) continue;
        int one = 1;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        struct timeval tv = { .tv_sec = 5 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ESP_LOGI(TAG, "client connected");
        int64_t t0 = esp_timer_get_time();
        int frames = 0;
        while (got_ip && read_frame(c)) {
            if (++frames % 100 == 0) {
                int64_t now = esp_timer_get_time();
                ESP_LOGI(TAG, "%.1f frames/s", 100e6 / (double)(now - t0));
                t0 = now;
            }
        }
        close(c);
        ESP_LOGI(TAG, "client gone after %d frames", frames);
        shown_up = false;                           // redraw the status screen
    }
}

void netfb_start(esp_lcd_panel_handle_t panel, uint8_t *fb0, uint8_t *fb1)
{
    g_panel = panel; g_front = fb0; g_back = fb1;
    g_lock = xSemaphoreCreateMutex();
    memset(fb0, 0, FRAME_PIXELS);
    memset(fb1, 0, FRAME_PIXELS);
    xTaskCreatePinnedToCore(net_task, "net", 8192, NULL, 5, NULL, 1);   // keep Wi-Fi off the display core
}
