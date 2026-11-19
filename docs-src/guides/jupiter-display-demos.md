---
title: Jupiter display demo suite
summary: Send monochrome phosphor versions of Jupiter's web demos to an MDA display.
order: 35
icon: monitor
audience: users
status: draft
---

The Jupiter demo suite presents a set of small static screens based on the web admin demo: overview, analytics, alerts, orders, invoice, network, settings and warehouse operations. They use the MDA's native phosphor intensity levels; the monitor itself supplies its green color.

## Run the suite

Build and flash the firmware in mode 3, connect the board to Wi-Fi, then send the screens from a computer on the same network:

```sh
python3 tools/jupiter_display.py 192.168.2.166 --demo all --fps 1 --repeat
```

The script sends compressed 2-bpp frames over the firmware's TCP frame protocol. Stop it with Ctrl-C. To show one screen once, choose a name and omit `--repeat`:

```sh
python3 tools/jupiter_display.py 192.168.2.166 --demo overview
python3 tools/jupiter_display.py 192.168.2.166 --demo warehouse
```

Available screens: `overview`, `analytics`, `alerts`, `orders`, `invoice`, `network`, `settings`, and `warehouse`.

## Preview a screen

Render a screen to a portable graymap without connecting to a board:

```sh
python3 tools/jupiter_display.py --demo analytics --preview /tmp/analytics.pgm
```

The preview uses gray values for the three display intensities; on hardware those intensities appear as green phosphor. Pillow is optional. If installed, provide the Jupiter SGI Text and SGI Alt font files to use them for body and display text:

```sh
python3 tools/jupiter_display.py 192.168.2.166 --demo all --repeat \
  --font /path/to/sgi-text.ttf --font-bold /path/to/sgi-alt.ttf
```

Without Pillow or font files, the script uses a compact built-in bitmap fallback. The web demo screens are adapted compositions for 720 × 350, not screenshots of the browser UI; dense controls and color-only distinctions are replaced with readable text, outlines and phosphor intensity.

## Display setup

The board must be running firmware mode 3 and show its IP address. If it is not configured for Wi-Fi, set the SSID and password through `idf.py menuconfig` under the Monochrome menu. See [[guides/send-frames-over-wifi]] and [[reference/frame-protocol]].
