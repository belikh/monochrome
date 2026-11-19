---
title: How the video works
summary: How the ESP32-S3 makes an MDA signal, and the two settings that must not change.
order: 10
icon: lightbulb
audience: developers
status: draft
---

The ESP32-S3 has an RGB LCD peripheral that reads pixels by DMA and outputs them with sync pulses. The firmware uses it as an MDA video source. The monitor takes video, intensity, H-sync and V-sync, and needs no pixel clock, so the firmware sets `pclk_gpio_num` and `de_gpio_num` to -1.

## Frame buffers

The 720 x 350 frame buffer is 252,000 bytes. The firmware offers two layouts:

| Layout | Used by | Frame buffer | Bounce buffer |
| --- | --- | --- | --- |
| Internal RAM | Modes 0 and 1 | One, in internal RAM | None |
| PSRAM | Mode 2 | Two, in PSRAM | Two of 35 lines each, in internal RAM |

With the PSRAM layout the driver copies the frame buffer into the [[term:Bounce buffer]] pair in small chunks, and the DMA reads the bounce buffers. The PSRAM layout leaves most of the internal RAM free and lets a program draw into one buffer while the DMA shows the other.

## Keep the bounce chunk count even

The number of bounce chunks per frame must be even. The firmware checks this at compile time in `main/main.c`.

| Bounce size | Chunks per frame | Result on the test monitor |
| --- | --- | --- |
| 10 lines | 35 (odd) | Picture bounced vertically and showed doubled lines |
| 70 lines | 5 (odd) | Picture steady, but bands of 70 lines repeated or swapped |
| 35 lines | 10 (even) | Steady and correct |

The V-sync interval measured 20,072 to 20,073 microseconds in every frame in all of these cases, so the sync timing was never the problem. The scrambled bands show that the data order was wrong. The driver alternates two bounce buffers and restarts the DMA from the first buffer at each V-sync. An odd chunk count leaves the two out of step at the end of a frame. This explanation fits the three results above. The sizes 25 lines and 5 lines (also an even count) were not tested.

The options `CONFIG_LCD_RGB_RESTART_IN_VSYNC`, `CONFIG_LCD_RGB_ISR_IRAM_SAFE` and 64-byte data cache lines did not fix the odd-count cases, and the firmware does not use them.

## Reuse a frame buffer only after the driver latches the new one

With two frame buffers, a program draws into the hidden one and calls `esp_lcd_panel_draw_bitmap` with it. That call does not wait. The driver switches to the new buffer once per frame, when it has copied the last chunk of the frame into the bounce buffers. It then calls `on_frame_buf_complete`. The old buffer is free to overwrite only after that call.

Waiting one V-sync after the flip is not enough. If the flip happens in the last few dozen lines of a frame, the driver reads the old buffer for one more frame, and a program that writes into it shows torn lines. The firmware counts the `on_frame_buf_complete` calls in `g_latched` and waits for the count to advance before it writes (`wait_latched` in `main/main.c`).

The Wi-Fi receiver exposed this. Its first version blocked for up to a frame after each flip, so most UDP packets overflowed the receive queue (about 9 of 50 packets per frame arrived). Waiting lazily, only when the next frame starts, raised that to 40 to 49 of 50 and removed the visible tearing.

## Keep V-sync active low

MDA uses an active-low V-sync and an active-high H-sync. When the firmware sent an active-high V-sync, the monitor used for development changed its picture size and brightened the raster. That suggests the monitor selects a CGA-style mode from the V-sync polarity. The firmware sets `vsync_idle_low = false` to keep the MDA mode.

## Hercules

A Hercules card sends the same kind of signal: the same DE-9 pins, the same TTL levels and the same sync polarities. A Hercules picture is 720x348 at a pixel clock of about 16 MHz. A monitor that accepts MDA accepts it too. The firmware does not emulate the Hercules memory layout.

:::see-also
- [[reference/frame-protocol]]
- [[concepts/how-the-3d-demo-works]]
- [[reference/timing]]
- [[term:MDA]]
:::
