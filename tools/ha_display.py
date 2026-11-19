#!/usr/bin/env python3
"""Home Assistant panels and an original retro-futurist executive animation.

Examples:
  python3 tools/ha_display.py 192.168.2.166 --demo all --repeat
  python3 tools/ha_display.py 192.168.2.166 --demo house --fps 8 --repeat
  python3 tools/ha_display.py --demo energy --preview /tmp/energy.pgm

Home Assistant integration is read-only. Set HA_TOKEN to a long-lived access token
and pass --ha-url to fetch live sensor states. Without credentials, screens use
clearly-labelled sample values. The character animation is an original homage to
retro-futurist casino-computer art, not a reproduction of a Fallout character.
"""
import argparse
import json
import math
import os
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from jupiter_display import Canvas, DEMOS, HEIGHT, WIDTH, send_frame

INK, BRIGHT = 1, 3

# Home Assistant entity defaults. Override through HA_ENTITY_<KEY> environment variables.
ENTITY_DEFAULTS = {
    "temperature": "sensor.home_temperature",
    "humidity": "sensor.home_humidity",
    "solar": "sensor.solar_power",
    "grid": "sensor.grid_power",
    "battery": "sensor.battery_soc",
    "house": "sensor.house_power",
    "presence": "person.homeowner",
    "weather": "weather.home",
}


def entity_id(key):
    return os.environ.get("HA_ENTITY_" + key.upper(), ENTITY_DEFAULTS[key])


def fetch_entity(base_url, token, key):
    """Fetch one HA state using the documented read-only REST API."""
    url = base_url.rstrip("/") + "/api/states/" + urllib.parse.quote(entity_id(key), safe=".")
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + token, "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=3) as response:
        data = json.loads(response.read().decode("utf-8"))
    return data.get("state", "unknown"), data.get("attributes", {})


def state_value(states, key, fallback):
    if key not in states:
        return fallback
    value, _attrs = states[key]
    return value


def number(states, key, fallback):
    try:
        return float(state_value(states, key, fallback))
    except (TypeError, ValueError):
        return fallback


def ha_status(c, states=None, sample=False):
    states = states or {}
    c.header("HOME ASSISTANT", 1, 4)
    c.title("HOME STATUS", "HOME ASSISTANT  /  " + ("LIVE READ-ONLY" if states else "SAMPLE DATA"))
    temp = number(states, "temperature", 21.6)
    humidity = number(states, "humidity", 48)
    presence = str(state_value(states, "presence", "home")).upper()
    weather = str(state_value(states, "weather", "partly cloudy")).replace("_", " ").upper()
    solar = number(states, "solar", 2.4)
    grid = number(states, "grid", -0.8)
    battery = number(states, "battery", 76)
    c.panel(30, 124, 204, 90, "INDOOR CLIMATE")
    c.text(48, 160, f"{temp:.1f} C", 4, BRIGHT, display=True)
    c.text(48, 194, f"HUMIDITY {humidity:.0f}%", 1)
    c.panel(250, 124, 204, 90, "PRESENCE")
    c.text(268, 164, presence[:12], 3, BRIGHT)
    c.text(268, 194, "PERSON STATE", 1)
    c.panel(470, 124, 220, 90, "WEATHER")
    c.text(488, 160, weather[:16], 2, BRIGHT)
    c.text(488, 192, f"OUTSIDE {number(states, 'temperature', 18.0):.0f} C", 1)
    c.panel(30, 230, 660, 78, "ENERGY  /  LIVE POWER")
    c.text(52, 267, f"SOLAR {solar:.1f} KW", 2, BRIGHT)
    c.text(280, 267, f"GRID {grid:+.1f} KW", 2, INK)
    c.text(500, 267, f"BATTERY {battery:.0f}%", 2, BRIGHT)
    c.footer("SAMPLE VALUES" if not states else "HOME ASSISTANT REST API  /  READ ONLY")


def energy_flow(c, states=None, frame=0):
    states = states or {}
    solar = max(0.0, number(states, "solar", 2.4))
    grid = number(states, "grid", -0.8)
    battery = number(states, "battery", 76)
    house = max(0.0, number(states, "house", 1.7))
    c.header("HOME ASSISTANT", 2, 4)
    c.title("ENERGY FLOW", "POWER BALANCE  /  " + ("LIVE" if states else "SAMPLE"))
    # Animate small phosphor packets along the three energy paths.
    nodes = {"SOLAR": (112, 160), "GRID": (608, 160), "HOME": (360, 250), "BATTERY": (360, 126)}
    paths = [("SOLAR", "HOME"), ("GRID", "HOME"), ("HOME", "BATTERY")]
    for a, b in paths:
        ax, ay = nodes[a]; bx, by = nodes[b]
        c.line(ax, ay, bx, by, INK)
        for p in (0.25, 0.55, 0.85):
            q = (p + frame * 0.035) % 1.0
            x, y = round(ax + (bx - ax) * q), round(ay + (by - ay) * q)
            c.rect(x - 2, y - 2, 5, 5, BRIGHT, True)
    values = {"SOLAR": f"{solar:.1f} KW", "GRID": f"{grid:+.1f} KW", "HOME": f"{house:.1f} KW", "BATTERY": f"{battery:.0f}%"}
    for name, (x, y) in nodes.items():
        c.rect(x - 58, y - 18, 116, 38, BRIGHT if name == "HOME" else INK)
        c.text(x - 49, y - 11, name, 1, BRIGHT)
        c.text(x - 49, y + 4, values[name], 1, INK)
    c.footer("PACKETS SHOW DIRECTION  /  BRIGHTNESS SHOWS EMPHASIS")


def climate_room(c, states=None, frame=0):
    states = states or {}
    temp = number(states, "temperature", 21.6)
    humidity = number(states, "humidity", 48)
    c.header("HOME ASSISTANT", 3, 4)
    c.title("CLIMATE", "ROOM SENSOR  /  " + ("LIVE" if states else "SAMPLE"))
    c.panel(30, 124, 316, 184, "TEMPERATURE")
    c.text(58, 158, f"{temp:.1f}", 6, BRIGHT, display=True); c.text(258, 207, "C", 3, BRIGHT)
    c.text(60, 264, "TARGET  20.0 - 22.0 C", 1)
    c.panel(366, 124, 324, 184, "HUMIDITY")
    c.text(402, 158, f"{humidity:.0f}%", 6, BRIGHT, display=True)
    # Compact trend trace, with a gentle animated cursor.
    vals = [34, 39, 37, 45, 42, 48, 51, 46, 53, 55, 49, 58, 52, 61, 56, 64]
    for i in range(len(vals) - 1):
        x0, x1 = 392 + i * 17, 392 + (i + 1) * 17
        c.line(x0, 278 - vals[i] // 2, x1, 278 - vals[i + 1] // 2, BRIGHT if i == frame % (len(vals) - 1) else INK)
    c.text(392, 290, "LAST 8 HOURS  /  RELATIVE HUMIDITY", 1)
    c.footer("SENSOR HISTORY  /  ROOM: LIVING")


def house_frame(c, frame=0):
    """Original retro-futurist executive portrait with gentle loop animation."""
    c.header("JUPITER / RETRO FUTURE", 4, 4)
    c.text(40, 54, "HOUSE SYSTEM", 4, BRIGHT, display=True)
    c.text(42, 96, "CASINO NETWORK  /  EXECUTIVE TERMINAL", 2, INK)
    # CRT-like ornamental frame and animated sweep line.
    c.rect(38, 118, 326, 190, INK)
    c.rect(44, 124, 314, 178, INK)
    # Abstract, non-photoreal face built from geometric silhouette and line work.
    c.rect(112, 143, 166, 139, BRIGHT)
    c.rect(126, 157, 138, 108, INK, True)
    # hair / crown and jaw outline
    c.line(133, 171, 148, 151, BRIGHT); c.line(148, 151, 221, 151, BRIGHT); c.line(221, 151, 251, 170, BRIGHT)
    c.line(132, 178, 132, 226, INK); c.line(132, 226, 151, 255, INK); c.line(151, 255, 232, 255, INK); c.line(232, 255, 254, 226, INK)
    # Eyes blink briefly every 48 animation frames.
    blink = frame % 48 in (0, 1)
    if blink:
        c.line(154, 197, 181, 197, BRIGHT); c.line(207, 197, 234, 197, BRIGHT)
    else:
        c.rect(157, 190, 22, 12, BRIGHT, True); c.rect(209, 190, 22, 12, BRIGHT, True)
        c.px(168, 195, BLACK); c.px(220, 195, BLACK)
    # Animated mouth: subtle speech bars with changing width.
    mouth_w = (20, 32, 44, 32)[(frame // 3) % 4]
    c.line(194 - mouth_w // 2, 231, 194 + mouth_w // 2, 231, BRIGHT)
    # Moving scanline; slight discrete movement keeps the animation CRT-like.
    scan_y = 132 + (frame * 3) % 162
    c.line(47, scan_y, 355, scan_y, BRIGHT)
    # Speech/status side panel.
    c.panel(390, 118, 300, 190, "EXECUTIVE STATUS")
    messages = ("NETWORK STABLE", "MARKET RISING", "CASINO ONLINE", "ALL SYSTEMS READY")
    c.text(410, 158, messages[(frame // 8) % len(messages)], 2, BRIGHT)
    c.text(410, 198, "HOUSE NET  /  04:21:08", 1)
    c.line(410, 220, 668, 220, INK)
    pulse = (frame % 32) / 31
    c.rect(410, 237, 250, 8, INK)
    c.rect(410, 237, max(2, round(250 * pulse)), 8, BRIGHT, True)
    c.text(410, 260, "LIVE TRANSMISSION", 1)
    c.footer("ORIGINAL RETRO-FUTURIST CHARACTER  /  NOT CANON ART")


def load_live_states(base_url, token, keys):
    states = {}
    for key in keys:
        try:
            states[key] = fetch_entity(base_url, token, key)
        except (OSError, ValueError, urllib.error.URLError, json.JSONDecodeError) as exc:
            print(f"warning: could not read {entity_id(key)}: {exc}", file=sys.stderr)
    return states


def render(name, c, states, frame):
    if name == "ha": ha_status(c, states)
    elif name == "energy": energy_flow(c, states, frame)
    elif name == "climate": climate_room(c, states, frame)
    elif name == "house": house_frame(c, frame)
    elif name in DEMOS: DEMOS[name](c)


def main():
    demos = [*DEMOS, "ha", "energy", "climate", "house", "all"]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", help="Monochrome board IP; omit when using --preview")
    ap.add_argument("--port", type=int, default=5050)
    ap.add_argument("--demo", choices=demos, default="ha")
    ap.add_argument("--fps", type=float, default=2, help="screen/frame updates per second")
    ap.add_argument("--repeat", action="store_true", help="keep cycling the selected screens")
    ap.add_argument("--preview", metavar="FILE.pgm", help="write one frame to a PGM preview")
    ap.add_argument("--ha-url", help="Home Assistant base URL, e.g. http://homeassistant.local:8123")
    ap.add_argument("--ha-token", default=os.environ.get("HA_TOKEN"), help="read-only long-lived access token; prefer HA_TOKEN env var")
    args = ap.parse_args()
    if not args.preview and not args.host: ap.error("HOST is required unless --preview is used")
    if args.fps <= 0: ap.error("--fps must be greater than zero")
    if args.ha_token and not args.ha_url: ap.error("--ha-url is required when using an HA token")
    names = ["ha", "energy", "climate", "house"] if args.demo == "all" else [args.demo]
    if args.preview and len(names) != 1: ap.error("--preview needs one named --demo, not all")
    live_keys = ("temperature", "humidity", "solar", "grid", "battery", "house", "presence", "weather")
    states = load_live_states(args.ha_url, args.ha_token, live_keys) if args.ha_url and args.ha_token else {}
    frame = 0
    try:
        if args.preview:
            c = Canvas(); render(names[0], c, states, frame)
            with open(args.preview, "wb") as out: out.write(c.pgm())
            print(f"wrote {names[0]} preview to {args.preview}")
            return
        with socket.create_connection((args.host, args.port), timeout=10) as sock:
            print(f"Home Assistant / retro-futurist MDA demos -> {args.host}:{args.port}")
            last_poll = time.monotonic()
            while True:
                for name in names:
                    if args.ha_url and args.ha_token and time.monotonic() - last_poll >= 5:
                        states = load_live_states(args.ha_url, args.ha_token, live_keys)
                        last_poll = time.monotonic()
                    c = Canvas(); render(name, c, states, frame); send_frame(sock, c.pack_2bpp())
                    frame += 1
                    print(f"showing {name}" if frame == 1 or args.fps <= 1 else f"\rshowing {name} frame {frame}", end="\n" if frame == 1 or args.fps <= 1 else "", flush=True)
                    time.sleep(1.0 / args.fps)
                if not args.repeat: break
    except KeyboardInterrupt:
        print("\nstopped")
    except OSError as exc:
        sys.exit(f"I/O error: {exc}")

if __name__ == "__main__":
    main()
