---
title: How the video works
summary: How the ESP32-S3 makes an MDA signal, and the two settings that must not change.
order: 10
icon: lightbulb
audience: developers
status: draft
---

The ESP32-S3 has an RGB LCD peripheral that reads pixels by DMA and outputs them with sync pulses. The firmware uses it as an MDA video source. The monitor takes video, intensity, H-sync and V-sync, and needs no pixel clock, so the firmware sets `pclk_gpio_num` and `de_gpio_num` to -1.

## Keep the framebuffer in internal RAM

The 720 x 350 framebuffer is 252,000 bytes and fits in the internal RAM of the S3. The firmware sets `.flags = { .fb_in_psram = false }` and does not use a bounce buffer.

An earlier version kept the framebuffer in PSRAM and used a bounce buffer of 10 lines. On the monitor used to develop this firmware, that version produced a picture that bounced vertically and showed doubled lines. The sync outputs measured correctly at the chip in both versions. Moving the framebuffer to internal RAM removed the bounce. The Espressif documentation describes late refills of the bounce buffer as a cause of shifted images.

:::warning
Do not move the framebuffer back to PSRAM without checking the picture on your monitor.
:::

## Keep V-sync active low

MDA uses an active-low V-sync and an active-high H-sync. When the firmware sent an active-high V-sync, the monitor used for development changed its picture size and brightened the raster. That suggests the monitor selects a CGA-style mode from the V-sync polarity. The firmware sets `vsync_idle_low = false` to keep the MDA mode.

## Hercules

A Hercules card sends the same kind of signal: the same DE-9 pins, the same TTL levels and the same sync polarities. A Hercules picture is 720x348 at a pixel clock of about 16 MHz. A monitor that accepts MDA accepts it too. The firmware does not emulate the Hercules memory layout.

:::see-also
- [[reference/timing]]
- [[term:MDA]]
:::
