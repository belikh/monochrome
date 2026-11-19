#!/usr/bin/env python3
"""Send frames to the Monochrome firmware (mode 3) over TCP or UDP.

  send.py HOST --demo [--udp] [--fmt 1bpp|2bpp|8bpp] [--fps 25] [--zlib] [--delta]
  send.py HOST --image picture.png [--udp] [--fmt 1bpp|2bpp]      (needs Pillow)
  send.py HOST --raw frame.bin --fmt 1bpp|2bpp|8bpp [--udp]

--zlib compresses frames (TCP) or rows (UDP). --delta, UDP only, sends just the rows that changed.
Only the standard library is needed for --demo and --raw. See docs-src/reference/frame-protocol.md.
"""
import argparse, socket, struct, sys, time, zlib

W, H = 720, 350
ROW = {"8bpp": W, "1bpp": W // 8, "2bpp": W // 4}
FMT = {"8bpp": 0, "1bpp": 1, "2bpp": 2}


def tcp_frame(fmt, payload, z):
    if z:
        body = zlib.compress(payload, 1)
        return b"MONO" + bytes([1, FMT[fmt], 1, 0]) + struct.pack("<I", len(body)) + body
    return b"MONO" + bytes([1, FMT[fmt], 0, 0]) + payload


MAX_PKT = 1400          # bytes of rows per datagram, after compression
MAX_RAW = 63000         # the device inflates at most this many bytes per packet
_rows_per_packet = {}   # remembered between frames so that packing does not search from scratch


def udp_packets(fmt, payload, seq, z, prev=None):
    """Packs a frame into datagrams. With prev, only rows that differ from it are sent."""
    row = ROW[fmt]
    max_rows = min(255, MAX_RAW // row)
    if prev is None:
        runs = [(0, H)]
    else:
        runs, start = [], None
        for y in range(H):
            same = payload[y * row:(y + 1) * row] == prev[y * row:(y + 1) * row]
            if not same and start is None:
                start = y
            elif same and start is not None:
                runs.append((start, y - start)); start = None
        if start is not None:
            runs.append((start, H - start))
    parts = []
    n = _rows_per_packet.get(fmt, max(1, MAX_PKT // row))
    for start, count in runs:
        r, end = start, start + count
        while r < end:
            k = max(1, min(n, end - r, max_rows))
            while True:
                chunk = payload[r * row:(r + k) * row]
                body, zf = chunk, 0
                if z:
                    c = zlib.compress(chunk, 1)
                    if len(c) < len(chunk):
                        body, zf = c, 2
                if len(body) <= MAX_PKT or k == 1:
                    break
                k = max(1, k * 3 // 4)
            n = min(max_rows, k * 5 // 4 + 1) if len(body) < MAX_PKT * 2 // 3 else k
            parts.append((r, k, zf, body))
            r += k
    _rows_per_packet[fmt] = n
    out = []
    for i, (r, k, zf, body) in enumerate(parts):
        flags = zf | (1 if i == len(parts) - 1 else 0)
        out.append(b"MU" + bytes([1, FMT[fmt]]) + struct.pack("<HH", seq & 0xFFFF, r) + bytes([k, flags]) + body)
    return out


# --- demo frames (standard library only): scrolling diagonal bars --------------------------------
# Every row is one of 48 shifts of the same pattern, in two variants (bars, or bright-only bars), so
# the rows are built once and each frame is a join of 350 prebuilt rows.

_PERIOD = 48
_rows = {}


def _pack(px, fmt):
    if fmt == "8bpp":
        return bytes(0x03 if v == 2 else v for v in px)
    out = bytearray()
    if fmt == "2bpp":
        for i in range(0, W, 4):
            out.append(px[i] << 6 | px[i + 1] << 4 | px[i + 2] << 2 | px[i + 3])
    else:
        for i in range(0, W, 8):
            b = 0
            for j in range(8):
                b = b << 1 | (1 if px[i + j] else 0)
            out.append(b)
    return bytes(out)


def _build_rows(fmt):
    base = [0 if (x % _PERIOD) < 16 else (1 if (x % _PERIOD) < 32 else 2) for x in range(W + _PERIOD)]
    rows = []
    for variant in (0, 1):
        for shift in range(_PERIOD):
            px = base[shift:shift + W]
            if variant:
                px = [2 if v else 0 for v in px]
            rows.append(_pack(px, fmt))
    _rows[fmt] = rows


def demo_frame(fmt, t):
    if fmt not in _rows:
        _build_rows(fmt)
    rows = _rows[fmt]
    return b"".join(rows[((y // 25) % 2) * _PERIOD + (y // 2 + t * 3) % _PERIOD] for y in range(H))


def image_frame(path, fmt):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("--image needs Pillow (pip install pillow); --demo and --raw do not")
    im = Image.open(path).convert("L").resize((W, H))
    if fmt == "1bpp":
        im = im.convert("1")                                # Floyd-Steinberg dither
        data = im.tobytes()
        return data
    levels = im.point(lambda v: 0 if v < 64 else (1 if v < 160 else 2))   # 2bpp: black, normal, bright
    px = list(levels.getdata())
    if fmt == "8bpp":
        return bytes(0x03 if v == 2 else v for v in px)
    out = bytearray()
    for i in range(0, len(px), 4):
        out.append(px[i] << 6 | px[i + 1] << 4 | px[i + 2] << 2 | px[i + 3])
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=5050)
    ap.add_argument("--udp", action="store_true")
    ap.add_argument("--fmt", choices=FMT, default="2bpp")
    ap.add_argument("--fps", type=float, default=25)
    ap.add_argument("--zlib", action="store_true", help="compress frames (TCP) or rows (UDP)")
    ap.add_argument("--delta", action="store_true", help="UDP only: send only rows that changed")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--demo", action="store_true")
    g.add_argument("--image")
    g.add_argument("--raw")
    a = ap.parse_args()

    static = None
    if a.image:
        static = image_frame(a.image, a.fmt)
    elif a.raw:
        static = open(a.raw, "rb").read()
        need = ROW[a.fmt] * H
        if len(static) != need:
            sys.exit(f"{a.raw} is {len(static)} bytes, {a.fmt} needs {need}")

    if a.delta and not a.udp:
        sys.exit("--delta needs --udp")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM if a.udp else socket.SOCK_STREAM)
    if a.udp:
        sock.connect((a.host, a.port))
    else:
        sock.connect((a.host, a.port))
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    print(f"sending {a.fmt} frames to {a.host}:{a.port} over {'UDP' if a.udp else 'TCP'}"
          f"{', compressed' if a.zlib else ''}{', changed rows only' if a.delta else ''}; Ctrl-C stops")
    t, sent, t0, nbytes, prev = 0, 0, time.time(), 0, None
    try:
        while True:
            start = time.time()
            payload = static if static is not None else demo_frame(a.fmt, t)
            if a.udp:
                pkts = udp_packets(a.fmt, payload, t, a.zlib, prev if a.delta else None)
                for pkt in pkts:
                    sock.send(pkt)
                    nbytes += len(pkt)
                    time.sleep(0.0002)                      # a short gap keeps the receiver's queue from overflowing
                prev = payload
            else:
                data = tcp_frame(a.fmt, payload, a.zlib)
                sock.sendall(data)
                nbytes += len(data)
            t += 1; sent += 1
            if sent % 50 == 0:
                el = time.time() - t0
                print(f"{sent / el:.1f} frames/s, {nbytes / sent / 1024:.1f} KB per frame, {nbytes / el / 1e6:.2f} MB/s")
            delay = 1.0 / a.fps - (time.time() - start)
            if delay > 0:
                time.sleep(delay)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
