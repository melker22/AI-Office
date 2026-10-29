#!/usr/bin/env python3
"""Decode an 8-bit PNG with the Python standard library only (zlib + struct).

Shared by wave 26 paint judges. No Pillow, no new shell.nix dependency.
Supports grayscale, RGB, and RGBA, non-interlaced, filters 0–4.
"""

from __future__ import annotations

import struct
import sys
import zlib

_SIG = b"\x89PNG\r\n\x1a\n"


def _paeth(left: int, up: int, up_left: int) -> int:
    estimate = left + up - up_left
    dl = abs(estimate - left)
    du = abs(estimate - up)
    dul = abs(estimate - up_left)
    if dl <= du and dl <= dul:
        return left
    if du <= dul:
        return up
    return up_left


def _unfilter(raw: bytes, width: int, height: int, bpp: int) -> bytes:
    stride = width * bpp
    expected = height * (stride + 1)
    if len(raw) < expected:
        raise ValueError(f"png scanlines truncated ({len(raw)} < {expected})")
    out = bytearray(height * stride)
    prev = bytearray(stride)
    index = 0
    for y in range(height):
        filt = raw[index]
        index += 1
        row = bytearray(raw[index : index + stride])
        index += stride
        if filt == 0:
            pass
        elif filt == 1:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + left) & 255
        elif filt == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif filt == 3:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + ((left + prev[x]) // 2)) & 255
        elif filt == 4:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                up = prev[x]
                up_left = prev[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + _paeth(left, up, up_left)) & 255
        else:
            raise ValueError(f"png filter {filt} is not supported")
        out[y * stride : (y + 1) * stride] = row
        prev = row
    return bytes(out)


def _to_rgba(samples: bytes, width: int, height: int, channels: int) -> bytes:
    if channels == 4:
        return samples
    out = bytearray(width * height * 4)
    if channels == 3:
        for i in range(width * height):
            s = i * 3
            d = i * 4
            out[d : d + 3] = samples[s : s + 3]
            out[d + 3] = 255
        return bytes(out)
    if channels == 1:
        for i, value in enumerate(samples):
            d = i * 4
            out[d : d + 3] = bytes((value, value, value))
            out[d + 3] = 255
        return bytes(out)
    if channels == 2:
        for i in range(width * height):
            s = i * 2
            d = i * 4
            value = samples[s]
            out[d : d + 3] = bytes((value, value, value))
            out[d + 3] = samples[s + 1]
        return bytes(out)
    raise ValueError(f"png channel count {channels} is not supported")


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    """Return width, height, and packed RGBA8 bytes, row-major, unpremultiplied."""
    if len(data) < 8 or data[:8] != _SIG:
        raise ValueError("not a png")
    offset = 8
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()
    while offset + 8 <= len(data):
        (length,) = struct.unpack(">I", data[offset : offset + 4])
        offset += 4
        tag = data[offset : offset + 4]
        offset += 4
        chunk = data[offset : offset + length]
        offset += length
        offset += 4  # crc
        if len(chunk) != length:
            raise ValueError("png chunk truncated")
        if tag == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", chunk)
            if comp != 0 or filt != 0:
                raise ValueError("png compression or filter method is not supported")
            if interlace != 0:
                raise ValueError("interlaced png is not supported")
            if bit_depth != 8:
                raise ValueError(f"png bit depth {bit_depth} is not supported")
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
    if width is None or not idat:
        raise ValueError("png is missing IHDR or IDAT")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise ValueError(f"png color type {color_type} is not supported")
    raw = zlib.decompress(bytes(idat))
    samples = _unfilter(raw, width, height, channels)
    return width, height, _to_rgba(samples, width, height, channels)


def _box(width: int, height: int, box: tuple[int, int, int, int] | None) -> tuple[int, int, int, int]:
    if box is None:
        return 0, 0, width, height
    x0, y0, x1, y1 = box
    return max(0, x0), max(0, y0), min(width, x1), min(height, y1)


def count_close(
    rgba: bytes,
    width: int,
    height: int,
    rgb: tuple[int, int, int],
    tol: float,
    box: tuple[int, int, int, int] | None = None,
) -> int:
    """Count pixels whose RGB is within Euclidean `tol` of `rgb`."""
    x0, y0, x1, y1 = _box(width, height, box)
    if x1 <= x0 or y1 <= y0:
        return 0
    tol2 = tol * tol
    tr, tg, tb = rgb
    found = 0
    for y in range(y0, y1):
        row = y * width * 4
        for x in range(x0, x1):
            i = row + x * 4
            dr = rgba[i] - tr
            dg = rgba[i + 1] - tg
            db = rgba[i + 2] - tb
            if dr * dr + dg * dg + db * db <= tol2:
                found += 1
    return found


def bbox_close(
    rgba: bytes,
    width: int,
    height: int,
    rgb: tuple[int, int, int],
    tol: float,
    box: tuple[int, int, int, int] | None = None,
) -> tuple[int, int, int, int] | None:
    """Inclusive-exclusive bbox of pixels within `tol` of `rgb`, or None."""
    x0, y0, x1, y1 = _box(width, height, box)
    tol2 = tol * tol
    tr, tg, tb = rgb
    minx, miny, maxx, maxy = width, height, -1, -1
    for y in range(y0, y1):
        row = y * width * 4
        for x in range(x0, x1):
            i = row + x * 4
            dr = rgba[i] - tr
            dg = rgba[i + 1] - tg
            db = rgba[i + 2] - tb
            if dr * dr + dg * dg + db * db <= tol2:
                if x < minx:
                    minx = x
                if y < miny:
                    miny = y
                if x + 1 > maxx:
                    maxx = x + 1
                if y + 1 > maxy:
                    maxy = y + 1
    if maxx < 0:
        return None
    return minx, miny, maxx, maxy


def count_where(
    rgba: bytes,
    width: int,
    height: int,
    pred,
    box: tuple[int, int, int, int] | None = None,
) -> int:
    x0, y0, x1, y1 = _box(width, height, box)
    found = 0
    for y in range(y0, y1):
        row = y * width * 4
        for x in range(x0, x1):
            i = row + x * 4
            if pred(rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]):
                found += 1
    return found


def bbox_where(
    rgba: bytes,
    width: int,
    height: int,
    pred,
    box: tuple[int, int, int, int] | None = None,
) -> tuple[int, int, int, int] | None:
    x0, y0, x1, y1 = _box(width, height, box)
    minx, miny, maxx, maxy = width, height, -1, -1
    for y in range(y0, y1):
        row = y * width * 4
        for x in range(x0, x1):
            i = row + x * 4
            if not pred(rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]):
                continue
            if x < minx:
                minx = x
            if y < miny:
                miny = y
            if x + 1 > maxx:
                maxx = x + 1
            if y + 1 > maxy:
                maxy = y + 1
    if maxx < 0:
        return None
    return minx, miny, maxx, maxy


def _chunk(tag: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)


def encode_rgba_png(width: int, height: int, rgba: bytes) -> bytes:
    """Filter-0 RGBA PNG. Used by the self-test, not by the app."""
    if len(rgba) != width * height * 4:
        raise ValueError("rgba size does not match width*height")
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride : (y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return _SIG + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(bytes(raw))) + _chunk(b"IEND", b"")


def self_test() -> None:
    width, height = 3, 2
    rgba = bytearray(width * height * 4)
    # (0,0) red, (1,0) green, (2,0) blue, second row white / black / yellow.
    pixels = [
        (255, 0, 0, 255),
        (0, 200, 0, 255),
        (0, 0, 255, 255),
        (255, 255, 255, 255),
        (0, 0, 0, 255),
        (255, 255, 0, 255),
    ]
    for i, (r, g, b, a) in enumerate(pixels):
        rgba[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
    decoded_w, decoded_h, decoded = decode_png(encode_rgba_png(width, height, bytes(rgba)))
    if (decoded_w, decoded_h, decoded) != (width, height, bytes(rgba)):
        raise SystemExit("png round-trip mismatch")
    # Sub filter on a 2-pixel RGB row: stored deltas, reconstructed absolute.
    raw = bytes([1, 10, 20, 30, 5, 6, 7])
    straight = _unfilter(raw, 2, 1, 3)
    if straight != bytes((10, 20, 30, 15, 26, 37)):
        raise SystemExit(f"sub filter mismatch: {list(straight)}")
    reds = count_close(decoded, width, height, (255, 0, 0), 1)
    if reds != 1:
        raise SystemExit(f"expected 1 red pixel, got {reds}")
    box = bbox_close(decoded, width, height, (0, 200, 0), 1)
    if box != (1, 0, 2, 1):
        raise SystemExit(f"green bbox {box}")
    try:
        decode_png(b"not a png")
    except ValueError:
        pass
    else:
        raise SystemExit("non-png was accepted")
    print("ok   png_probe")


if __name__ == "__main__":
    if "--self-test" in sys.argv[1:]:
        self_test()
    else:
        raise SystemExit("usage: png_probe.py --self-test")
