#pragma once
#include <stdint.h>
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Runs the shaded 3D torus-knot demo forever. fb0/fb1 are the panel's two PSRAM frame buffers
// (720x350 bytes each). vsync_sem is given once per frame by the panel's on_vsync callback.
void knot_run(esp_lcd_panel_handle_t panel, uint8_t *fb0, uint8_t *fb1, SemaphoreHandle_t vsync_sem)
    __attribute__((noreturn));
