#pragma once
#include <stdint.h>
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Joins Wi-Fi and shows frames sent over TCP (see docs-src/reference/frame-protocol.md).
// fb0/fb1 are the panel's two PSRAM frame buffers (720x350 bytes each). Starts tasks on core 1 and
// returns.
void netfb_start(esp_lcd_panel_handle_t panel, uint8_t *fb0, uint8_t *fb1);
