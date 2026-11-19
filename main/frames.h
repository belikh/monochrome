#pragma once
#include <stdint.h>

// The RGB driver latches which frame buffer it reads next once per frame. After a call to
// esp_lcd_panel_draw_bitmap() the buffer that was on screen is only free to overwrite after the
// next latch. g_latched counts latches; wait_latched(n) blocks until the count is above n.
extern volatile uint32_t g_latched;
void wait_latched(uint32_t since);
