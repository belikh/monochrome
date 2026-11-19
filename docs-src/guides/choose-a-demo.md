---
title: Choose a demo
summary: Switch between firmware graphics and the Jupiter green-phosphor demo suite.
order: 30
icon: sliders
audience: users
status: draft
---

The firmware has four modes. You choose one with the `MODE` macro in `main/main.c`.

| `MODE` | Shows | Frame buffers |
| --- | --- | --- |
| 3 (default) | A picture that a program sends over Wi-Fi. Until one arrives, the monitor shows the board's IP address. | Two, in PSRAM |
| 2 | A shaded 3D torus knot with an orbiting light, at 49 fps | Two, in PSRAM |
| 1 | A wireframe icosahedron that tumbles and bounces around the screen | One, in internal RAM |
| 0 | A static calibration pattern: borders, grid, diagonals, intensity blocks, checkerboard and circle | One, in internal RAM |

To change the mode:

1. Edit the line `#define MODE 3` in `main/main.c`.
2. Rebuild and flash. See [[start/build-and-flash]].

Mode 3 needs a Wi-Fi network. See [[guides/send-frames-over-wifi]]. For Jupiter's adapted green-phosphor web demo screens, run `python3 tools/jupiter_display.py <board-ip> --demo all --fps 1 --repeat`. See [[guides/jupiter-display-demos]].

Mode 2 prints its frame rate and the time spent in each stage every 100 frames, for example `49.3 fps, 17.0 ms render per frame, 813 of 1536 triangles drawn`.

Use mode 0 to centre the picture. See [[guides/adjust-the-picture]].

:::see-also
- [[guides/send-frames-over-wifi]]
- [[guides/jupiter-display-demos]]
- [[concepts/how-the-3d-demo-works]]
- [[guides/adjust-the-picture]]
:::
