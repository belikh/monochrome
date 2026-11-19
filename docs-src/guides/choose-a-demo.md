---
title: Choose a demo
summary: Switch between the 3D knot, the wireframe screensaver and the calibration pattern.
order: 30
icon: sliders
audience: users
status: draft
---

The firmware has three modes. You choose one with the `MODE` macro in `main/main.c`.

| `MODE` | Shows | Frame buffers |
| --- | --- | --- |
| 2 (default) | A shaded 3D torus knot with an orbiting light, at 49 fps | Two, in PSRAM |
| 1 | A wireframe icosahedron that tumbles and bounces around the screen | One, in internal RAM |
| 0 | A static calibration pattern: borders, grid, diagonals, intensity blocks, checkerboard and circle | One, in internal RAM |

To change the mode:

1. Edit the line `#define MODE 2` in `main/main.c`.
2. Rebuild and flash. See [[start/build-and-flash]].

Mode 2 prints its frame rate and the time spent in each stage every 100 frames, for example `49.3 fps, 17.0 ms render per frame, 813 of 1536 triangles drawn`.

Use mode 0 to centre the picture. See [[guides/adjust-the-picture]].

:::see-also
- [[concepts/how-the-3d-demo-works]]
- [[guides/adjust-the-picture]]
:::
