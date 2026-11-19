---
title: Wire the monitor
summary: Connect four GPIOs and ground to the DE-9 connector of a TTL monochrome monitor.
order: 10
icon: monitor
audience: users
status: draft
---

Connect the board to the monitor's DE-9 connector as listed in [[reference/pins]].

1. Connect both ground pins (1 and 2) of the connector to a board `GND` pin.
2. Connect GPIO 6 to pin 8 (H-sync) and GPIO 7 to pin 9 (V-sync).
3. Connect GPIO 4 to pin 7 (video).
4. Connect GPIO 5 to pin 6 (intensity), or leave pin 6 unconnected if you want one brightness level.

The GPIOs output 3.3 V and the monitor expects TTL levels. The setup this firmware was developed on drives the monitor directly from the GPIOs, with no buffer and no resistors, and the picture is stable. If your monitor misbehaves, put a 74HCT244 or 74AHCT125 buffer, or 150 ohm series resistors, between the GPIOs and the connector.

:::warning
Do not move the signals to GPIO 33 to 37 on an N16R8 board, because the octal PSRAM uses them. Avoid the strapping pins (0, 3, 45, 46) and the USB pins (19, 20).
:::

:::see-also
- [[reference/pins]]
- [[start/build-and-flash]]
:::
