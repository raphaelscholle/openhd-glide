#!/usr/bin/env python3
import argparse
import math
import pathlib
import struct
import zlib


def web_mercator_tile(lat, lon, zoom):
    lat = max(-85.05112878, min(85.05112878, lat))
    lat_rad = math.radians(lat)
    n = 1 << zoom
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    return x, y


def web_mercator_pixel(lat, lon, zoom, size):
    lat = max(-85.05112878, min(85.05112878, lat))
    lat_rad = math.radians(lat)
    scale = size * (1 << zoom)
    return (
        (lon + 180.0) / 360.0 * scale,
        (1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) / 2.0 * scale,
    )


def png_chunk(kind, data):
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def write_png(path, width, height, pixels):
    rows = []
    for y in range(height):
        row = bytearray([0])
        for x in range(width):
            r, g, b = pixels[y * width + x]
            row.extend((r, g, b))
        rows.append(bytes(row))
    raw = b"".join(rows)
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    data += png_chunk(b"IDAT", zlib.compress(raw, 6))
    data += png_chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def tile_pixels(tile_x, tile_y, size, zoom):
    def hash2(ix, iy):
        value = (ix * 374761393 + iy * 668265263 + tile_x * 1442695041 + tile_y * 22695477) & 0xFFFFFFFF
        value = (value ^ (value >> 13)) * 1274126177 & 0xFFFFFFFF
        return ((value ^ (value >> 16)) & 0xFFFF) / 65535.0

    def smoothstep(t):
        return t * t * (3.0 - 2.0 * t)

    def noise(wx, wy, scale):
        gx = math.floor(wx / scale)
        gy = math.floor(wy / scale)
        tx = smoothstep((wx / scale) - gx)
        ty = smoothstep((wy / scale) - gy)
        a = hash2(gx, gy)
        b = hash2(gx + 1, gy)
        c = hash2(gx, gy + 1)
        d = hash2(gx + 1, gy + 1)
        return (a * (1.0 - tx) + b * tx) * (1.0 - ty) + (c * (1.0 - tx) + d * tx) * ty

    def mix(a, b, t):
        t = max(0.0, min(1.0, t))
        return [int(a[i] * (1.0 - t) + b[i] * t) for i in range(3)]

    def put(buffer, x, y, color):
        if 0 <= x < size and 0 <= y < size:
            buffer[y * size + x] = tuple(color)

    def line(buffer, x0, y0, x1, y1, width, color):
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        error = dx + dy
        radius = max(0, width // 2)
        while True:
            for oy in range(-radius, radius + 1):
                for ox in range(-radius, radius + 1):
                    if ox * ox + oy * oy <= radius * radius:
                        put(buffer, x0 + ox, y0 + oy, color)
            if x0 == x1 and y0 == y1:
                break
            twice = error * 2
            if twice >= dy:
                error += dy
                x0 += sx
            if twice <= dx:
                error += dx
                y0 += sy

    pixels = []
    for y in range(size):
        for x in range(size):
            wx = tile_x * size + x
            wy = tile_y * size + y
            elevation = noise(wx, wy, 520.0) * 0.48 + noise(wx + 91.0, wy - 37.0, 190.0) * 0.32 + noise(wx - 19.0, wy + 143.0, 68.0) * 0.20
            forest = noise(wx + 600.0, wy - 180.0, 155.0)
            wet = noise(wx - 310.0, wy + 760.0, 260.0)
            slope = abs(noise(wx + 4.0, wy, 115.0) - noise(wx - 4.0, wy, 115.0)) + abs(noise(wx, wy + 4.0, 115.0) - noise(wx, wy - 4.0, 115.0))

            if elevation < 0.27 and wet > 0.52:
                color = mix([17, 51, 74], [27, 102, 112], elevation / 0.27)
            elif elevation < 0.36:
                color = mix([49, 77, 47], [85, 112, 58], (elevation - 0.20) / 0.16)
            elif elevation < 0.62:
                color = mix([37, 79, 45], [99, 120, 63], (elevation - 0.36) / 0.26)
            elif elevation < 0.80:
                color = mix([118, 111, 74], [140, 132, 100], (elevation - 0.62) / 0.18)
            else:
                color = mix([143, 143, 134], [213, 209, 186], (elevation - 0.80) / 0.20)

            if forest > 0.58 and 0.31 < elevation < 0.72:
                color = mix(color, [20, 63, 35], min(0.55, (forest - 0.58) * 1.8))
            if slope > 0.10:
                color = mix(color, [11, 23, 20], min(0.35, slope * 1.8))
            if abs((elevation * 21.0) % 1.0 - 0.5) < 0.018 and elevation > 0.32:
                color = mix(color, [183, 208, 176], 0.22)
            pixels.append(tuple(color))
    return pixels


def main():
    parser = argparse.ArgumentParser(description="Generate a tiny offline PNG tile set for the LVGL minimap demo.")
    parser.add_argument("--root", default="assets/maps")
    parser.add_argument("--zoom", type=int, default=15)
    parser.add_argument("--lat", type=float, default=38.8976763)
    parser.add_argument("--lon", type=float, default=-77.0365298)
    parser.add_argument("--radius", type=int, default=2)
    parser.add_argument("--size", type=int, default=256)
    args = parser.parse_args()

    center_x, center_y = web_mercator_tile(args.lat, args.lon, args.zoom)
    root = pathlib.Path(args.root)
    for y in range(center_y - args.radius, center_y + args.radius + 1):
        for x in range(center_x - args.radius, center_x + args.radius + 1):
            path = root / str(args.zoom) / str(x) / f"{y}.png"
            write_png(path, args.size, args.size, tile_pixels(x, y, args.size, args.zoom))
            print(path)


if __name__ == "__main__":
    main()
