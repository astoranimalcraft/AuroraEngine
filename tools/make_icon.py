#!/usr/bin/env python3
"""Draws the default NexiumEngine logo and embeds it in the library.

Produces two files:

    assets/NexiumEngine.png        the logo itself, for docs and for anyone who
                                   wants to look at it
    src/internal/icon_data.h       the same image as raw RGBA8, so the window can
                                   build an HICON without reading a file and a
                                   program gets a taskbar icon with no assets

Everything is drawn here, in code, so the logo has no external source and can be
regenerated on any machine with a stock Python. Only zlib and struct are used:
PNG is simple enough to write directly, and that keeps the build free of an image
library dependency.

Run from the repository root:

    python3 tools/make_icon.py

The generated header is committed, like shader_sources.h, so the build needs no
Python.
"""
import math
import os
import struct
import sys
import zlib

SIZE = 256          # final icon edge, in pixels
SS = 4              # supersampling factor; the image is drawn at SIZE*SS and box-filtered

OUT_PNG = os.path.join("assets", "NexiumEngine.png")
OUT_HEADER = os.path.join("src", "internal", "icon_data.h")


# ------------------------------------------------------------------ drawing --

def rounded_rect(x, y, w, h, radius):
    """Signed-ish test: is (x, y) inside the rounded rectangle? Returns coverage
    in [0, 1], using a small linear ramp at the edge for anti-aliasing."""
    cx = min(max(x, 0.0), 1.0)
    # distance outside the rectangle, measured per axis
    dx = max(0.0 - x, 0.0, x - w)
    dy = max(0.0 - y, 0.0, y - h)
    d = math.hypot(dx, dy)
    if d > 0.0:
        return 0.0
    # inside: check the corners
    qx = max(0.0 - x, 0.0, x - w)
    qy = max(0.0 - y, 0.0, y - h)
    if qx > 0.0 and qy > 0.0:
        return 0.0
    # distance to the rounded corner region
    cxc = radius if x < radius else (w - radius if x > w - radius else x)
    cyc = radius if y < radius else (h - radius if y > h - radius else y)
    if x < radius and y < radius:
        dist = math.hypot(x - radius, y - radius)
    elif x > w - radius and y < radius:
        dist = math.hypot(x - (w - radius), y - radius)
    elif x < radius and y > h - radius:
        dist = math.hypot(x - radius, y - (h - radius))
    elif x > w - radius and y > h - radius:
        dist = math.hypot(x - (w - radius), y - (h - radius))
    else:
        return 1.0
    return 1.0 if dist <= radius else 0.0


def point_in_polygon(px, py, poly):
    """Even-odd rule ray cast."""
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > py) != (yj > py):
            xint = (xj - xi) * (py - yi) / (yj - yi) + xi
            if px < xint:
                inside = not inside
        j = i
    return inside


def poly_coverage(x, y, poly):
    return 1.0 if point_in_polygon(x, y, poly) else 0.0


def thick_segment_coverage(x, y, ax, ay, bx, by, half_width):
    """Coverage of a capsule (thick line segment) with a rounded cap."""
    vx, vy = bx - ax, by - ay
    wx, wy = x - ax, y - ay
    length_sq = vx * vx + vy * vy
    t = 0.0 if length_sq == 0.0 else max(0.0, min(1.0, (wx * vx + wy * vy) / length_sq))
    px, py = ax + t * vx, ay + t * vy
    return 1.0 if math.hypot(x - px, y - py) <= half_width else 0.0


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def over(base, top, alpha):
    """Source-over composite of `top` at `alpha` onto `base` (both RGB tuples)."""
    return tuple(base[i] + (top[i] - base[i]) * alpha for i in range(3))


def render():
    """Draws one supersampled RGBA image as a flat list of floats."""
    w = SIZE * SS
    pixels = [0.0] * (w * w * 4)

    # Geometry, in normalized 0..1 units of the icon square.
    pad = 0.045
    rect_w = 1.0 - 2 * pad
    radius = 0.20 * rect_w

    # The letter N: two stems and a diagonal.
    stem_w = 0.115
    left_x0 = 0.305
    right_x1 = 0.695
    top_y = 0.255
    bottom_y = 0.745

    # The orbit ring, an ellipse tilted across the mark.
    ring_cx, ring_cy = 0.5, 0.5
    ring_rx, ring_ry = 0.415, 0.255
    ring_angle = math.radians(-22.0)
    ring_half = 0.019

    ca, sa = math.cos(ring_angle), math.sin(ring_angle)

    # The two electrons ride the ring.
    def electron(t):
        ex = ring_rx * math.cos(t)
        ey = ring_ry * math.sin(t)
        return (ring_cx + ex * ca - ey * sa, ring_cy + ex * sa + ey * ca)

    e1 = electron(math.radians(35.0))
    e2 = electron(math.radians(215.0))

    top_color = (46, 96, 176)
    bottom_color = (12, 14, 30)
    mark_color = (238, 244, 255)
    ring_color = (96, 214, 255)

    for py in range(w):
        y = (py + 0.5) / w
        for px in range(w):
            x = (px + 0.5) / w
            i = (py * w + px) * 4

            # --- background plate ---
            bg = rounded_rect(x - pad, y - pad, rect_w, rect_w, radius)
            if bg <= 0.0:
                continue

            # vertical gradient, biased so the glow sits behind the mark
            t = (x * 0.35 + y * 0.65)
            color = mix(top_color, bottom_color, min(1.0, max(0.0, t)))

            # a soft radial glow behind the centre
            glow = math.hypot(x - 0.5, y - 0.46)
            glow_a = max(0.0, 1.0 - glow / 0.62) ** 2 * 0.30
            color = over(color, (70, 140, 235), glow_a)

            # --- orbit ring (behind the letter) ---
            # Rotate into ring space and measure the ellipse distance.
            dx, dy = x - ring_cx, y - ring_cy
            rx = dx * ca + dy * sa
            ry = -dx * sa + dy * ca
            if ring_rx > 0 and ring_ry > 0:
                k = math.hypot(rx / ring_rx, ry / ring_ry)
                # approximate distance to the ellipse along the radial direction
                if k > 1e-6:
                    ex_, ey_ = rx / k, ry / k
                    dist = math.hypot(rx - ex_, ry - ey_)
                else:
                    dist = ring_rx
                if dist <= ring_half:
                    color = over(color, ring_color, 0.95)

            # --- the letter N ---
            n = 0.0
            if left_x0 <= x <= left_x0 + stem_w and top_y <= y <= bottom_y:
                n = 1.0
            elif right_x1 - stem_w <= x <= right_x1 and top_y <= y <= bottom_y:
                n = 1.0
            else:
                n = thick_segment_coverage(
                    x, y,
                    left_x0 + stem_w * 0.5, top_y,
                    right_x1 - stem_w * 0.5, bottom_y,
                    stem_w * 0.5)

            if n > 0.0:
                color = over(color, mark_color, n)

            # --- electrons ---
            for (exx, eyy) in (e1, e2):
                d = math.hypot(x - exx, y - eyy)
                if d <= 0.030:
                    color = over(color, (255, 255, 255), 0.98)

            pixels[i + 0] = color[0]
            pixels[i + 1] = color[1]
            pixels[i + 2] = color[2]
            pixels[i + 3] = 255.0 * bg

    return pixels, w


def downsample(pixels, w):
    """Box-filters the supersampled image down to SIZE x SIZE RGBA8 bytes."""
    out = bytearray(SIZE * SIZE * 4)
    inv = 1.0 / (SS * SS)
    for y in range(SIZE):
        for x in range(SIZE):
            r = g = b = a = 0.0
            for sy in range(SS):
                row = ((y * SS + sy) * w + x * SS) * 4
                for sx in range(SS):
                    i = row + sx * 4
                    # premultiply so transparent pixels do not darken the edges
                    alpha = pixels[i + 3] / 255.0
                    r += pixels[i + 0] * alpha
                    g += pixels[i + 1] * alpha
                    b += pixels[i + 2] * alpha
                    a += pixels[i + 3]
            a *= inv
            alpha = a / 255.0
            o = (y * SIZE + x) * 4
            if alpha > 1e-6:
                out[o + 0] = max(0, min(255, int(r * inv / alpha + 0.5)))
                out[o + 1] = max(0, min(255, int(g * inv / alpha + 0.5)))
                out[o + 2] = max(0, min(255, int(b * inv / alpha + 0.5)))
            out[o + 3] = max(0, min(255, int(a + 0.5)))
    return bytes(out)


# -------------------------------------------------------------------- output --

def write_png(path, rgba, width, height):
    def chunk(tag, data):
        payload = tag + data
        return struct.pack(">I", len(data)) + payload + struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 (None)
        raw += rgba[y * width * 4:(y + 1) * width * 4]

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", header)
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    return len(png)


def write_header(path, rgba, width, height):
    lines = []
    lines.append("// NexiumEngine — generated. Do not edit by hand.")
    lines.append("//")
    lines.append("// The default window icon: assets/NexiumEngine.png as raw RGBA8, so the")
    lines.append("// engine can build an HICON with no file on disk and no image decoder.")
    lines.append("// Regenerate with tools/make_icon.py.")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace n3d::internal {")
    lines.append("")
    lines.append(f"inline constexpr int kIconWidth = {width};")
    lines.append(f"inline constexpr int kIconHeight = {height};")
    lines.append("")
    lines.append("inline constexpr uint8_t kIconPixels[] = {")

    per_line = 16
    for i in range(0, len(rgba), per_line):
        chunk = rgba[i:i + per_line]
        lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    lines.append("};")
    lines.append("")
    lines.append(f"inline constexpr unsigned kIconPixelCount = {len(rgba)}u;")
    lines.append("")
    lines.append("} // namespace n3d::internal")
    lines.append("")

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return len(rgba)


def main():
    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    pixels, w = render()
    rgba = downsample(pixels, w)

    png_bytes = write_png(OUT_PNG, rgba, SIZE, SIZE)
    raw_bytes = write_header(OUT_HEADER, rgba, SIZE, SIZE)

    print(f"wrote {OUT_PNG} ({png_bytes} bytes, {SIZE}x{SIZE})")
    print(f"wrote {OUT_HEADER} ({raw_bytes} bytes of RGBA)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
