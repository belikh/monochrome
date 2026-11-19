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

## Home Assistant and animated screens

A companion sender includes Home Assistant status, energy flow, climate and an animated original retro-futurist executive character. It uses sample data by default; optional Home Assistant integration reads states only and never calls services:

```sh
python3 tools/ha_display.py 192.168.2.166 --demo all --fps 2 --repeat
python3 tools/ha_display.py 192.168.2.166 --demo house --fps 8 --repeat
```

For live Home Assistant values, create a long-lived access token in your Home Assistant profile and keep it out of shell history and source control. Set it in the environment, then pass the instance URL:

```sh
export HA_TOKEN='your-long-lived-access-token'
python3 tools/ha_display.py 192.168.2.166 --ha-url http://homeassistant.local:8123 --demo all --repeat
```

The default entity IDs are `sensor.home_temperature`, `sensor.home_humidity`, `sensor.solar_power`, `sensor.grid_power`, `sensor.battery_soc`, `sensor.house_power`, `person.homeowner` and `weather.home`. Override any key with `HA_ENTITY_TEMPERATURE`, `HA_ENTITY_HUMIDITY`, `HA_ENTITY_SOLAR`, `HA_ENTITY_GRID`, `HA_ENTITY_BATTERY`, `HA_ENTITY_HOUSE`, `HA_ENTITY_PRESENCE` or `HA_ENTITY_WEATHER`. The integration calls Home Assistant's read-only `/api/states/<entity_id>` endpoint and polls at a low rate. Without credentials, screens display sample data.

The animated character is original retro-futurist casino-computer artwork: a geometric face, blinking eyes, moving scan line and changing status text. It is not a reproduction of Fallout character artwork or dialogue.

## Preview a screen

Render a screen to a portable graymap without connecting to a board:

```sh
python3 tools/jupiter_display.py --demo analytics --preview /tmp/analytics.pgm
python3 tools/ha_display.py --demo house --preview /tmp/house.pgm
```

The preview uses gray values for the three display intensities; on hardware those intensities appear as green phosphor. Pillow is optional. If installed, provide the Jupiter SGI Text and SGI Alt font files to use them for body and display text:

```sh
python3 tools/jupiter_display.py 192.168.2.166 --demo all --repeat \
  --font /path/to/sgi-text.ttf --font-bold /path/to/sgi-alt.ttf
```

Without Pillow or font files, the Jupiter sender uses a compact built-in bitmap fallback. The web demo screens are adapted compositions for 720 × 350, not screenshots of the browser UI; dense controls and color-only distinctions are replaced with readable text, outlines and phosphor intensity.

## Display setup

The board must be running firmware mode 3 and show its IP address. If it is not configured for Wi-Fi, set the SSID and password through `idf.py menuconfig` under the Monochrome menu. See [[guides/send-frames-over-wifi]] and [[reference/frame-protocol]].
