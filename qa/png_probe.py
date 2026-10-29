#!/usr/bin/env python3
"""Decode a PNG with the Python standard library only (zlib + struct).

Shared by wave 26 paint judges. Another fleet node may write this file too;
keep the public function stable so integration can merge the copies.

Returns width, height, and row-major RGBA8 bytes. Interlaced images and bit
depths other than 8 are rejected. Color types: gray, gray+alpha, RGB, RGBA.
"""

from __future__ import annotations

import struct
import zlib

_SIG = b"\x89PNG\r\n\x1a\n"


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    """width, height, row-major RGBA8. stdlib zlib+struct only. Rejects interlaced."""
    if not isinstance(data, (bytes, bytearray)):
        raise ValueError("png data must be bytes")
    raw = bytes(data)
    if len(raw) < 8 or raw[:8] != _SIG:
        raise ValueError("not a png")
    pos = 8
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()
    while pos + 8 <= len(raw):
        length = struct.unpack(">I", raw[pos : pos + 4])[0]
        pos += 4
        ctype = raw[pos : pos + 4]
        pos += 4
        if pos + length + 4 > len(raw):
            raise ValueError("truncated png chunk")
        chunk = raw[pos : pos + length]
        pos += length
        pos += 4  # CRC, unchecked
        if ctype == b"IHDR":
            if length < 13:
                raise ValueError("short IHDR")
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", chunk[:13]
            )
            if comp != 0 or filt != 0:
                raise ValueError("unsupported png compression")
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break
    if width is None or height is None or bit_depth is None or color_type is None:
        raise ValueError("png missing IHDR")
    if interlace:
        raise ValueError("interlaced png is not supported")
    if bit_depth != 8:
        raise ValueError(f"unsupported bit depth {bit_depth}")
    if color_type == 0:
        channels = 1
    elif color_type == 2:
        channels = 3
    elif color_type == 4:
        channels = 2
    elif color_type == 6:
        channels = 4
    else:
        raise ValueError(f"unsupported color type {color_type}")
    if width <= 0 or height <= 0:
        raise ValueError("empty png")
    stride = width * channels
    try:
        inflated = zlib.decompress(bytes(idat))
    except zlib.error as exc:
        raise ValueError(f"png zlib: {exc}") from exc
    expected = height * (stride + 1)
    if len(inflated) < expected:
        raise ValueError("png image data is short")
    rows: list[bytearray] = []
    src = 0
    for y in range(height):
        filt_byte = inflated[src]
        src += 1
        row = bytearray(inflated[src : src + stride])
        src += stride
        prev = rows[y - 1] if y else None
        if filt_byte == 0:
            pass
        elif filt_byte == 1:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + left) & 0xFF
        elif filt_byte == 2:
            for i in range(stride):
                up = prev[i] if prev else 0
                row[i] = (row[i] + up) & 0xFF
        elif filt_byte == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                up = prev[i] if prev else 0
                row[i] = (row[i] + ((left + up) // 2)) & 0xFF
        elif filt_byte == 4:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                up = prev[i] if prev else 0
                ul = prev[i - channels] if prev and i >= channels else 0
                row[i] = (row[i] + _paeth(left, up, ul)) & 0xFF
        else:
            raise ValueError(f"unsupported png filter {filt_byte}")
        rows.append(row)
    out = bytearray(width * height * 4)
    o = 0
    for row in rows:
        if color_type == 2:
            for i in range(0, stride, 3):
                out[o : o + 4] = bytes((row[i], row[i + 1], row[i + 2], 255))
                o += 4
        elif color_type == 6:
            out[o : o + stride] = row
            o += stride
        elif color_type == 0:
            for i in range(stride):
                g = row[i]
                out[o : o + 4] = bytes((g, g, g, 255))
                o += 4
        else:
            for i in range(0, stride, 2):
                g = row[i]
                a = row[i + 1]
                out[o : o + 4] = bytes((g, g, g, a))
                o += 4
    return width, height, bytes(out)
