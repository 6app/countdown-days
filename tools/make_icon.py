#!/usr/bin/env python3
"""生成 res/app.ico（日历样式），只依赖标准库。

包含 16/20/24/32/40/48/64/256 多个尺寸，覆盖 100%~200% 缩放下的托盘和资源管理器图标。
用法: python3 tools/make_icon.py
"""
import os
import struct
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SS = 4  # 超采样倍数，用来抗锯齿


def rounded_rect_cov(px, py, x0, y0, x1, y1, r):
    """点 (px,py) 是否在圆角矩形内。"""
    if px < x0 or px > x1 or py < y0 or py > y1:
        return False
    cx = min(max(px, x0 + r), x1 - r)
    cy = min(max(py, y0 + r), y1 - r)
    return (px - cx) ** 2 + (py - cy) ** 2 <= r * r


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))


def shade(u, v):
    """在单位坐标 (u,v) ∈ [0,1) 处返回 RGBA(0..1)，未覆盖返回 None。"""
    # 外框：蓝色渐变圆角方块
    if not rounded_rect_cov(u, v, 0.04, 0.04, 0.96, 0.96, 0.20):
        return None
    top, bot = (0.36, 0.58, 0.98), (0.11, 0.31, 0.85)
    color = lerp(top, bot, v)
    # 白色日历纸
    if rounded_rect_cov(u, v, 0.20, 0.24, 0.80, 0.82, 0.08):
        color = (1.0, 1.0, 1.0)
        # 顶部橙色页眉
        if v < 0.40:
            color = (0.98, 0.45, 0.20)
        else:
            # 日期格子 3x2，第一格高亮
            gx = (u - 0.28) / 0.15
            gy = (v - 0.47) / 0.14
            ix, iy = int(gx), int(gy)
            if 0 <= ix < 3 and 0 <= iy < 2 and gx - ix < 0.70 and gy - iy < 0.68 and u >= 0.28 and v >= 0.47:
                color = (0.98, 0.45, 0.20) if (ix, iy) == (2, 1) else (0.78, 0.84, 0.95)
    # 两个日历环扣
    for cx in (0.36, 0.64):
        if rounded_rect_cov(u, v, cx - 0.035, 0.16, cx + 0.035, 0.31, 0.03):
            color = (0.95, 0.96, 0.98)
    return (*color, 1.0)


def render(size):
    """返回自上而下的 BGRA 像素（预乘前的直通 alpha）。"""
    out = bytearray()
    for y in range(size):
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SS):
                for sx in range(SS):
                    c = shade((x + (sx + 0.5) / SS) / size, (y + (sy + 0.5) / SS) / size)
                    if c:
                        acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 1
            n = SS * SS
            a = acc[3] / n
            if acc[3]:
                r, g, b = (acc[i] / acc[3] for i in range(3))
            else:
                r = g = b = 0
            out += bytes((round(b * 255), round(g * 255), round(r * 255), round(a * 255)))
    return bytes(out)


def bmp_entry(size, bgra):
    """ICO 内的 BMP 格式：BITMAPINFOHEADER + 自下而上像素 + AND 掩码。"""
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = [bgra[y * size * 4:(y + 1) * size * 4] for y in range(size)]
    pixels = b''.join(reversed(rows))
    mask_row = ((size + 31) // 32) * 4
    mask = bytes(mask_row * size)  # 全 0：由 alpha 决定透明
    return header + pixels + mask


def png_entry(size, bgra):
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        row = bgra[y * size * 4:(y + 1) * size * 4]
        for i in range(0, len(row), 4):
            b, g, r, a = row[i:i + 4]
            raw += bytes((r, g, b, a))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    ihdr = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')


def main():
    images = []
    for s in SIZES:
        px = render(s)
        images.append((s, png_entry(s, px) if s >= 256 else bmp_entry(s, px)))

    head = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    dirs, blobs = b'', b''
    for s, data in images:
        dim = 0 if s >= 256 else s
        dirs += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blobs += data

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'res', 'app.ico')
    with open(path, 'wb') as f:
        f.write(head + dirs + blobs)
    print('wrote', os.path.normpath(path))


if __name__ == '__main__':
    main()
