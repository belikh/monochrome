---
title: Pins
summary: GPIO to DE-9 pin mapping and the pixel byte format.
order: 10
icon: pin
audience: users
status: draft
---

| Signal | GPIO | DE-9 pin | Notes |
| --- | --- | --- | --- |
| Video | 4 | 7 | Pixel bit. 1 is on, 0 is off. |
| Intensity | 5 | 6 | Bright pixel bit. |
| H-sync | 6 | 8 | 18.43 kHz, active high. |
| V-sync | 7 | 9 | About 50 Hz, active low. |
| Ground | GND | 1 and 2 | Connect both pins. |

## Pixel values

The framebuffer holds one byte per pixel. Bit 0 drives the video pin and bit 1 drives the intensity pin.

| Byte | Result |
| --- | --- |
| `0x00` | Black |
| `0x01` | Normal brightness |
| `0x03` | Bright |

Each frame buffer is 720 x 350 bytes (252,000 bytes), indexed as `fb[y * 720 + x]`. Modes 2 and 3 have two frame buffers and modes 0 and 1 have one. The firmware sets the pins in `main/main.c` with `PIN_VIDEO`, `PIN_INTEN`, `PIN_HSYNC` and `PIN_VSYNC`.

:::see-also
- [[reference/timing]]
- [[guides/wire-the-monitor]]
:::
