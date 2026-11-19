---
title: Timing
summary: Pixel clock, porches and sync polarity of the 720x350 MDA signal.
order: 20
icon: clock
audience: developers
status: draft
---

The firmware configures the RGB LCD driver (`esp_lcd_rgb_panel_config_t`) in HV mode, with no data enable pin and no pixel clock pin.

| Setting | Value |
| --- | --- |
| Pixel clock (`pclk_hz`) | 16,257,000 |
| Horizontal active (`h_res`) | 720 |
| `hsync_front_porch` | 18 |
| `hsync_pulse_width` | 108 |
| `hsync_back_porch` | 36 |
| Horizontal total | 882 clocks (18.43 kHz) |
| Vertical active (`v_res`) | 350 |
| `vsync_front_porch` | 1 |
| `vsync_pulse_width` | 16 |
| `vsync_back_porch` | 3 |
| Vertical total | 370 lines (about 49.8 Hz) |
| `hsync_idle_low` | `true` (active-high H-sync) |
| `vsync_idle_low` | `false` (active-low V-sync) |
| `data_width` | 8, with only the two lowest bits wired |

Counting pulses on the sync pins with the pulse counter peripheral gave about 18.4 kHz for H-sync and 50 Hz for V-sync.

:::see-also
- [[concepts/how-the-video-works]]
- [[guides/adjust-the-picture]]
:::
