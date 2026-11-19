---
title: Build and flash
summary: Build the firmware with ESP-IDF 5.5 and flash it to an ESP32-S3-DevKitC-1.
order: 10
icon: terminal
audience: users
status: draft
---

You need ESP-IDF 5.5 and an ESP32-S3-DevKitC-1 with the N16R8 module (16 MB flash, 8 MB octal PSRAM).

## Build and flash

1. Load the ESP-IDF environment in your shell.

   ```sh title="Load ESP-IDF"
   . ~/esp/esp-idf/export.sh
   ```

2. Set the target and build.

   ```sh title="Build"
   idf.py set-target esp32s3
   idf.py build
   ```

3. Plug in the board and flash it. Use the port that your system shows for the board.

   ```sh title="Flash"
   idf.py -p /dev/ttyACM0 flash monitor
   ```

The monitor prints `MDA video running: 720x350, pclk 16257000 Hz, mode 2` when the video driver has started. The default mode shows a 3D torus knot. See [[guides/choose-a-demo]] for the others.

:::note
The DevKitC-1 has two USB ports. The port wired to the UART bridge carries the console log. Flashing worked through that port, and it showed up as `/dev/ttyACM0` on Linux.
:::

## Check the sync before you connect video

Wire the sync lines and ground first, as described in [[guides/wire-the-monitor]]. Leave the video wire unplugged and power the monitor. You should see a steady raster with no whine from the monitor. Connect the video wire once the monitor holds lock.

:::see-also
- [[guides/wire-the-monitor]]
- [[guides/choose-a-demo]]
- [[guides/adjust-the-picture]]
:::
