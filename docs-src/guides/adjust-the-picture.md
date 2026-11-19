---
title: Adjust the picture
summary: Centre the image and understand why it can be smaller than the screen.
order: 20
icon: sliders
audience: users
status: draft
---

The picture position depends on the [[term:Porch]] values in `main/main.c`. The picture size depends on the monitor.

## Centre the picture

To move the picture sideways, trade pixels between `hsync_front_porch` and `hsync_back_porch`. Keep their sum at 54. To move it vertically, trade lines between `vsync_front_porch` and `vsync_back_porch`. Keep their sum at 4.

| Setting | Default | Sum to keep |
| --- | --- | --- |
| `hsync_front_porch` | 18 | 54 with `hsync_back_porch` |
| `hsync_back_porch` | 36 | 54 with `hsync_front_porch` |
| `vsync_front_porch` | 1 | 4 with `vsync_back_porch` |
| `vsync_back_porch` | 3 | 4 with `vsync_front_porch` |

Rebuild and flash after each change. See [[start/build-and-flash]]. Set `MODE` to 0 to see the border and grid while you adjust. See [[guides/choose-a-demo]].

## Margin around the picture

On the INTRA monitor used to develop this firmware, a margin of about 2 cm remained around the grid. The signal is a standard 720x350 raster, and the monitor sets how much of the glass the picture covers. The monitor's front controls adjust brightness and contrast only.
