#!/usr/bin/env python3
"""Jupiter design demos for a green-phosphor MDA display.

Examples:
  python3 tools/jupiter_display.py 192.168.2.166 --demo overview
  python3 tools/jupiter_display.py 192.168.2.166 --demo all --fps 2
  python3 tools/jupiter_display.py --demo analytics --preview /tmp/analytics.pgm

The preview option writes a portable graymap (PGM), where brighter pixels map to
brighter phosphor. Network output uses Monochrome's compressed 2-bpp TCP protocol.
Pillow is optional; pass --font/--font-bold with Pillow installed to render using
Jupiter's SGI Text and SGI Alt font files.
"""
import argparse
import math
import socket
import struct
import sys
import time
import zlib

WIDTH, HEIGHT = 720, 350
INK = 1
BRIGHT = 3
BLACK = 0

# Small legible fallback for environments without Pillow or installed Jupiter fonts.
# These are only a preview fallback: use --font and --font-bold for Jupiter type.
FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "J": ("00111", "00010", "00010", "00010", "10010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "10000", "11110", "00001", "00001", "11110"),
    "6": ("01110", "10000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    ".": ("00000", "00000", "00000", "00000", "00000", "01100", "01100"),
    ",": ("00000", "00000", "00000", "00000", "00110", "00100", "01000"),
    ":": ("00000", "00100", "00100", "00000", "00100", "00100", "00000"),
    "%": ("11001", "11010", "00100", "01000", "10110", "00110", "00000"),
    "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
    "+": ("00000", "00100", "00100", "11111", "00100", "00100", "00000"),
    "/": ("00001", "00010", "00010", "00100", "01000", "01000", "10000"),
    "#": ("01010", "11111", "01010", "01010", "11111", "01010", "00000"),
    "_": ("00000", "00000", "00000", "00000", "00000", "00000", "11111"),
    "!": ("00100", "00100", "00100", "00100", "00100", "00000", "00100"),
    "?": ("01110", "10001", "00001", "00010", "00100", "00000", "00100"),
    "'": ("00100", "00100", "00000", "00000", "00000", "00000", "00000"),
    "(": ("00010", "00100", "01000", "01000", "01000", "00100", "00010"),
    ")": ("01000", "00100", "00010", "00010", "00010", "00100", "01000"),
    "[": ("01110", "01000", "01000", "01000", "01000", "01000", "01110"),
    "]": ("01110", "00010", "00010", "00010", "00010", "00010", "01110"),
    "=": ("00000", "11111", "00000", "11111", "00000", "00000", "00000"),
    " ": ("00000",) * 7,
}

class Canvas:
    def __init__(self, regular_font=None, display_font=None):
        self.pixels = bytearray(WIDTH * HEIGHT)
        self.fonts = None
        if regular_font or display_font:
            try:
                from PIL import ImageFont
            except ImportError as exc:
                raise SystemExit("--font requires Pillow (python3 -m pip install pillow)") from exc
            self.fonts = (ImageFont.truetype(regular_font, 18) if regular_font else None,
                          ImageFont.truetype(display_font, 30) if display_font else None)

    def px(self, x, y, value=INK):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            self.pixels[y * WIDTH + x] = value

    def line(self, x0, y0, x1, y1, value=INK):
        dx, sx = abs(x1 - x0), 1 if x0 < x1 else -1
        dy, sy = -abs(y1 - y0), 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.px(x0, y0, value)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy; x0 += sx
            if e2 <= dx:
                err += dx; y0 += sy

    def rect(self, x, y, w, h, value=INK, fill=False):
        if fill:
            for yy in range(max(0, y), min(HEIGHT, y + h)):
                start = max(0, x); end = min(WIDTH, x + w)
                if end > start:
                    self.pixels[yy * WIDTH + start:yy * WIDTH + end] = bytes([value]) * (end - start)
        else:
            self.line(x, y, x + w - 1, y, value); self.line(x, y + h - 1, x + w - 1, y + h - 1, value)
            self.line(x, y, x, y + h - 1, value); self.line(x + w - 1, y, x + w - 1, y + h - 1, value)

    def text(self, x, y, text, scale=2, value=INK, display=False):
        text = str(text).upper()
        font = self.fonts[1 if display else 0] if self.fonts else None
        if font:
            try:
                from PIL import Image, ImageDraw
                mask = Image.new("1", (WIDTH, HEIGHT))
                ImageDraw.Draw(mask).text((x, y), text, font=font, fill=1)
                data = mask.tobytes()
                for yy in range(HEIGHT):
                    for xx in range(WIDTH):
                        if data[yy * WIDTH + xx]: self.px(xx, yy, value)
                return
            except ImportError:
                pass
        cursor = x
        for ch in text:
            glyph = FONT.get(ch, FONT["?"])
            for gy, row in enumerate(glyph):
                for gx, on in enumerate(row):
                    if on == "1":
                        for sy in range(scale):
                            for sx in range(scale): self.px(cursor + gx * scale + sx, y + gy * scale + sy, value)
            cursor += 6 * scale

    def label(self, x, y, text, value=INK): self.text(x, y, text, 2, value)

    def panel(self, x, y, w, h, title):
        self.rect(x, y, w, h, INK)
        self.rect(x + 1, y + 1, w - 2, 24, INK, True)
        self.text(x + 12, y + 5, title, 2, BRIGHT)

    def header(self, section, page, total):
        self.text(24, 16, "JUPITER", 2, BRIGHT)
        self.text(164, 16, "/", 2, INK)
        self.text(184, 16, section, 2, INK)
        self.text(598, 16, f"{page:02d}/{total:02d}", 2, BRIGHT)
        self.line(24, 38, 696, 38, INK)

    def title(self, value, subtitle=""):
        self.text(30, 52, value, 4, BRIGHT, display=True)
        if subtitle: self.text(32, 92, subtitle, 2, INK)

    def footer(self, hint="JUPITER DESIGN SYSTEM  /  MDA"):
        self.line(24, 324, 696, 324, INK)
        self.text(28, 332, hint, 1, INK)

    def pack_2bpp(self):
        out = bytearray()
        for y in range(HEIGHT):
            row = self.pixels[y * WIDTH:(y + 1) * WIDTH]
            for x in range(0, WIDTH, 4):
                out.append((row[x] << 6) | (row[x + 1] << 4) | (row[x + 2] << 2) | row[x + 3])
        return bytes(out)

    def pgm(self):
        # PGM displays as neutral gray on a computer; the monitor renders its own green phosphor.
        shades = bytes((0, 130, 0, 255))
        return f"P5\n{WIDTH} {HEIGHT}\n255\n".encode() + bytes(shades[p] for p in self.pixels)


def screen_overview(c):
    c.header("OVERVIEW", 1, 8); c.title("SYSTEM OVERVIEW", "LIVE SERVICE SUMMARY  /  MONDAY 09:41")
    for i, (name, value, note) in enumerate((("UPTIME", "99.98%", "+0.03 THIS WEEK"), ("REQUESTS", "2.48M", "+8.2% VS LAST WEEK"), ("LATENCY", "142 MS", "P95  /  TARGET 200 MS"))):
        x = 30 + i * 224; c.panel(x, 124, 208, 78, name); c.text(x + 16, 158, value, 3, BRIGHT); c.text(x + 16, 188, note, 1, INK)
    c.panel(30, 218, 430, 92, "TRAFFIC  /  LAST 12 HOURS")
    vals = [22, 30, 25, 38, 32, 46, 40, 58, 52, 48, 65, 72, 60, 82, 76, 90, 70, 96]
    for i in range(len(vals) - 1): c.line(54 + i * 21, 292 - vals[i] // 2, 54 + (i + 1) * 21, 292 - vals[i + 1] // 2, BRIGHT if i > 12 else INK)
    c.panel(478, 218, 212, 92, "INCIDENTS"); c.text(496, 258, "01 OPEN", 3, BRIGHT); c.text(496, 292, "API / 6 MIN AGO", 1, INK)
    c.footer()

def screen_analytics(c):
    c.header("ANALYTICS", 2, 8); c.title("ANALYTICS", "REVENUE AND VISITORS  /  LAST 7 DAYS")
    c.panel(30, 124, 420, 184, "WEEKLY ACTIVITY")
    heights = [62, 94, 72, 122, 103, 148, 129]
    for i, h in enumerate(heights):
        x = 64 + i * 52; c.line(x, 282, x, 282 - h, BRIGHT if i == 5 else INK); c.line(x + 2, 282, x + 20, 282, INK)
        c.text(x - 2, 288, ("M T W T F S S".split()[i]), 1, INK)
    c.panel(468, 124, 222, 84, "NET REVENUE"); c.text(486, 160, "$84,290", 3, BRIGHT); c.text(486, 190, "+12.4%  /  7D", 1)
    c.panel(468, 224, 222, 84, "ACTIVE USERS"); c.text(486, 260, "12,804", 3, BRIGHT); c.text(486, 290, "PEAK  /  14:32", 1)
    c.footer()

def screen_alerts(c):
    c.header("ALERTS", 3, 8); c.title("ALERTS", "INCIDENT QUEUE  /  3 ITEMS")
    rows = [("P1", "PAYMENT API", "ERROR RATE 4.8%", "09:38"), ("P2", "MAIL RELAY", "DELIVERY DELAY", "09:21"), ("P3", "WORKER POOL", "CAPACITY 82%", "08:56")]
    for i, (sev, name, detail, tm) in enumerate(rows):
        y = 124 + i * 61; c.rect(30, y, 660, 50, BRIGHT if i == 0 else INK); c.text(44, y + 13, sev, 2, BRIGHT); c.text(96, y + 9, name, 2, BRIGHT if i == 0 else INK); c.text(96, y + 30, detail, 1, INK); c.text(614, y + 14, tm, 2, INK)
    c.footer("ENTER  OPEN INCIDENT  /  ESC  BACK")

def screen_orders(c):
    c.header("ORDERS", 4, 8); c.title("ORDERS", "RECENT ORDERS  /  128 TOTAL")
    c.panel(30, 124, 660, 184, "ORDER REGISTER")
    c.text(46, 158, "ORDER     CUSTOMER       STATUS       TOTAL", 2, BRIGHT)
    c.line(44, 178, 676, 178)
    data = [("#10482", "NORTHSTAR", "PAID", "$248.00"), ("#10481", "FIELDWORK", "PENDING", "$1,820.00"), ("#10480", "STUDIO N", "PAID", "$74.50"), ("#10479", "ORBITAL CO", "REVIEW", "$620.00")]
    for i, row in enumerate(data):
        y = 190 + i * 27; c.text(46, y, row[0], 1, BRIGHT if i == 0 else INK); c.text(174, y, row[1], 1); c.text(386, y, row[2], 1, BRIGHT if row[2] == "PAID" else INK); c.text(566, y, row[3], 1); c.line(44, y + 19, 676, y + 19)
    c.footer("4 SELECTED  /  F FILTER  /  ENTER OPEN")

def screen_invoice(c):
    c.header("BILLING", 5, 8); c.title("INVOICE  /  0248", "NORTHSTAR SYSTEMS  /  DUE 30 NOV 2026")
    c.panel(30, 124, 410, 184, "LINE ITEMS")
    items = [("JUPITER PLATFORM", "1  x  $240.00", "$240.00"), ("SUPPORT PLAN", "1  x  $80.00", "$80.00"), ("TAX", "10%", "$32.00")]
    for i, (name, qty, amount) in enumerate(items):
        y = 168 + i * 39; c.text(48, y, name, 2, INK); c.text(48, y + 18, qty, 1, INK); c.text(344, y + 7, amount, 2, BRIGHT if i == 2 else INK)
    c.panel(458, 124, 232, 184, "AMOUNT DUE"); c.text(478, 170, "$352.00", 3, BRIGHT); c.line(478, 209, 668, 209); c.text(478, 228, "STATUS  /  SENT", 2); c.text(478, 268, "[ SEND REMINDER ]", 1, BRIGHT)
    c.footer("INVOICE PREVIEW  /  PRINT READY")

def screen_network(c):
    c.header("NETWORK", 6, 8); c.title("NETWORK STATUS", "EDGE CLUSTER  /  REGION AU-EAST")
    nodes = [(108, 188, "EDGE 01", "ONLINE"), (304, 156, "API 01", "ONLINE"), (304, 238, "API 02", "ONLINE"), (514, 196, "DB PRIMARY", "REPLICA OK")]
    for a, b in ((0, 1), (0, 2), (1, 3), (2, 3)): c.line(nodes[a][0] + 58, nodes[a][1] + 18, nodes[b][0], nodes[b][1] + 18, INK)
    for x, y, name, state in nodes:
        c.rect(x, y, 122, 44, BRIGHT if name == "DB PRIMARY" else INK); c.text(x + 8, y + 7, name, 1, BRIGHT); c.text(x + 8, y + 26, state, 1)
    c.text(42, 290, "LATENCY  18 MS       PACKET LOSS  0.00%       LAST CHECK  09:41:06", 1, BRIGHT)
    c.footer()

def screen_settings(c):
    c.header("SETTINGS", 7, 8); c.title("PREFERENCES", "DISPLAY AND ACCOUNT SETTINGS")
    entries = [("THEME", "SYSTEM"), ("DENSITY", "COMFORTABLE"), ("REDUCED MOTION", "ON"), ("SESSION TIMEOUT", "30 MINUTES"), ("TWO-FACTOR AUTH", "ENABLED")]
    for i, (key, val) in enumerate(entries):
        y = 126 + i * 35; c.text(50, y, key, 2, BRIGHT if i == 1 else INK); c.text(340, y, val, 2, BRIGHT if i == 1 else INK); c.line(44, y + 25, 676, y + 25)
    c.rect(32, 155, 656, 35, BRIGHT)
    c.footer("UP/DOWN SELECT  /  ENTER CHANGE  /  ESC BACK")

def screen_warehouse(c):
    c.header("WAREHOUSE", 8, 8); c.title("DOCK OPERATIONS", "NORTH YARD  /  LIVE QUEUE")
    c.panel(30, 124, 306, 184, "WAITING QUEUE")
    for i, (name, load, eta) in enumerate((("A. MORGAN", "CHILLED", "04 MIN"), ("J. SINGH", "GENERAL", "09 MIN"), ("R. TAYLOR", "FROZEN", "12 MIN"))):
        y = 166 + i * 42; c.text(46, y, f"{i + 1:02d}  {name}", 2, BRIGHT if i == 0 else INK); c.text(70, y + 20, f"{load}  /  ETA {eta}", 1); c.line(44, y + 34, 320, y + 34)
    c.panel(356, 124, 334, 184, "DOCK STATUS")
    for i, (dock, status) in enumerate((("DOCK 01", "LOADING"), ("DOCK 02", "AVAILABLE"), ("DOCK 03", "AVAILABLE"))):
        y = 165 + i * 43; c.rect(376, y, 42, 28, BRIGHT if i == 0 else INK); c.text(434, y + 3, dock, 2); c.text(560, y + 3, status, 1, BRIGHT if i == 0 else INK)
    c.footer("QUEUE POSITION UPDATES AUTOMATICALLY")

DEMOS = {"overview": screen_overview, "analytics": screen_analytics, "alerts": screen_alerts, "orders": screen_orders, "invoice": screen_invoice, "network": screen_network, "settings": screen_settings, "warehouse": screen_warehouse}

def send_frame(sock, payload):
    body = zlib.compress(payload, 1)
    sock.sendall(b"MONO" + bytes((1, 2, 1, 0)) + struct.pack("<I", len(body)) + body)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", help="Monochrome board IP; omit when using --preview")
    ap.add_argument("--port", type=int, default=5050)
    ap.add_argument("--demo", choices=[*DEMOS, "all"], default="overview")
    ap.add_argument("--fps", type=float, default=1.0, help="screen updates per second when cycling (default: 1)")
    ap.add_argument("--repeat", action="store_true", help="keep cycling the selected demo or full suite")
    ap.add_argument("--preview", metavar="FILE.pgm", help="write one rendered screen as a PGM image and exit")
    ap.add_argument("--font", help="path to Jupiter SGI Text TTF/OTF (requires Pillow)")
    ap.add_argument("--font-bold", help="path to Jupiter SGI Alt TTF/OTF (requires Pillow)")
    args = ap.parse_args()
    if not args.preview and not args.host: ap.error("HOST is required unless --preview is used")
    if args.fps <= 0: ap.error("--fps must be greater than zero")
    names = list(DEMOS) if args.demo == "all" else [args.demo]
    if args.preview and len(names) != 1: ap.error("--preview needs one named --demo, not all")
    try:
        if args.preview:
            c = Canvas(args.font, args.font_bold); DEMOS[names[0]](c)
            with open(args.preview, "wb") as f: f.write(c.pgm())
            print(f"wrote {names[0]} preview to {args.preview}")
            return
        with socket.create_connection((args.host, args.port), timeout=10) as sock:
            print(f"Jupiter MDA demos -> {args.host}:{args.port} ({', '.join(names)})")
            while True:
                for name in names:
                    c = Canvas(args.font, args.font_bold); DEMOS[name](c); send_frame(sock, c.pack_2bpp())
                    print(f"showing {name}")
                    time.sleep(1.0 / args.fps)
                if not args.repeat: break
    except KeyboardInterrupt:
        print("\nstopped")
    except OSError as exc:
        sys.exit(f"I/O error: {exc}")

if __name__ == "__main__":
    main()
