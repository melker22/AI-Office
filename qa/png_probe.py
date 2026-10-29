#!/usr/bin/env python3
"""Decode PNG bytes with the Python standard library (zlib + struct).

Wave 26 layout judges share this helper. It has no dependency beyond the
standard library, so a Nix shell does not need a new image package. Another
wave-26 task may edit this file; keep the public functions stable:

- decode_png(data) -> (width, height, rgba)
- find_color_bbox(width, height, rgba, rgb, tolerance, region=None) -> box | None

rgba is row-major RGBA8 bytes, length width * height * 4.
A box is (left, top, right, bottom) with right/bottom exclusive.
tolerance is the maximum per-channel absolute difference.
"""

from __future__ import annotations

import struct
import zlib

_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(ValueError):
    pass


def _paeth(left: int, up: int, up_left: int) -> int:
    estimate = left + up - up_left
    dist_left = abs(estimate - left)
    dist_up = abs(estimate - up)
    dist_up_left = abs(estimate - up_left)
    if dist_left <= dist_up and dist_left <= dist_up_left:
        return left
    if dist_up <= dist_up_left:
        return up
    return up_left


def _unfilter(raw: bytes, height: int, stride: int, bpp: int) -> bytes:
    rows = bytearray()
    prior = bytearray(stride)
    offset = 0
    row_len = stride + 1
    if len(raw) < height * row_len:
        raise PngError("PNG image data is shorter than the header")
    for _y in range(height):
        filter_type = raw[offset]
        scan = raw[offset + 1 : offset + row_len]
        offset += row_len
        current = bytearray(stride)

        def left_at(index: int) -> int:
            return current[index - bpp] if index >= bpp else 0

        def up_left_at(index: int) -> int:
            return prior[index - bpp] if index >= bpp else 0

        if filter_type == 0:
            current[:] = scan
        elif filter_type == 1:
            for i, value in enumerate(scan):
                current[i] = (value + left_at(i)) & 0xFF
        elif filter_type == 2:
            for i, value in enumerate(scan):
                current[i] = (value + prior[i]) & 0xFF
        elif filter_type == 3:
            for i, value in enumerate(scan):
                current[i] = (value + ((left_at(i) + prior[i]) // 2)) & 0xFF
        elif filter_type == 4:
            for i, value in enumerate(scan):
                current[i] = (value + _paeth(left_at(i), prior[i], up_left_at(i))) & 0xFF
        else:
            raise PngError(f"unsupported PNG filter {filter_type}")
        rows += current
        prior = current
    return bytes(rows)


def _to_rgba(samples: bytes, width: int, height: int, color_type: int, palette: bytes, trns: bytes) -> bytes:
    count = width * height
    out = bytearray(count * 4)
    if color_type == 2:
        if len(samples) < count * 3:
            raise PngError("PNG RGB data is short")
        for i in range(count):
            out[i * 4 : i * 4 + 3] = samples[i * 3 : i * 3 + 3]
            out[i * 4 + 3] = 255
        return bytes(out)
    if color_type == 6:
        if len(samples) < count * 4:
            raise PngError("PNG RGBA data is short")
        return samples[: count * 4]
    if color_type == 0:
        if len(samples) < count:
            raise PngError("PNG gray data is short")
        for i, gray in enumerate(samples[:count]):
            out[i * 4 : i * 4 + 3] = bytes((gray, gray, gray))
            out[i * 4 + 3] = 255
        return bytes(out)
    if color_type == 4:
        if len(samples) < count * 2:
            raise PngError("PNG gray+alpha data is short")
        for i in range(count):
            gray = samples[i * 2]
            out[i * 4 : i * 4 + 3] = bytes((gray, gray, gray))
            out[i * 4 + 3] = samples[i * 2 + 1]
        return bytes(out)
    if color_type == 3:
        if not palette:
            raise PngError("PNG palette image has no PLTE")
        colors = len(palette) // 3
        alphas = list(trns) if trns else [255] * colors
        if len(alphas) < colors:
            alphas.extend([255] * (colors - len(alphas)))
        if len(samples) < count:
            raise PngError("PNG indexed data is short")
        for i, index in enumerate(samples[:count]):
            if index >= colors:
                raise PngError("PNG palette index out of range")
            out[i * 4 : i * 4 + 3] = palette[index * 3 : index * 3 + 3]
            out[i * 4 + 3] = alphas[index]
        return bytes(out)
    raise PngError(f"unsupported PNG color type {color_type}")


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    """Return width, height, and row-major RGBA8 bytes."""
    if not data.startswith(_SIGNATURE):
        raise PngError("not a PNG")
    offset = len(_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
    palette = b""
    trns = b""
    idat = bytearray()
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        offset += 4
        tag = data[offset : offset + 4]
        offset += 4
        chunk = data[offset : offset + length]
        offset += length
        offset += 4  # CRC
        if len(chunk) != length:
            raise PngError("truncated PNG chunk")
        if tag == b"IHDR":
            if length < 13:
                raise PngError("short IHDR")
            width, height, bit_depth, color_type, compression, png_filter, interlace = struct.unpack(
                ">IIBBBBB", chunk[:13]
            )
            if compression != 0 or png_filter != 0:
                raise PngError("unsupported PNG compression")
        elif tag == b"PLTE":
            palette = chunk
        elif tag == b"tRNS":
            trns = chunk
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
    if width is None or height is None or bit_depth is None or color_type is None:
        raise PngError("PNG has no IHDR")
    if interlace:
        raise PngError("interlaced PNG is not supported")
    if bit_depth != 8:
        raise PngError(f"unsupported PNG bit depth {bit_depth}")
    if width <= 0 or height <= 0:
        raise PngError("PNG has an empty image")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise PngError(f"unsupported PNG color type {color_type}")
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as exc:
        raise PngError("PNG zlib data is corrupt") from exc
    samples = _unfilter(raw, height, width * channels, channels)
    rgba = _to_rgba(samples, width, height, color_type, palette, trns)
    return width, height, rgba


def find_color_bbox(
    width: int,
    height: int,
    rgba: bytes,
    rgb: tuple[int, int, int],
    tolerance: int,
    region: tuple[int, int, int, int] | None = None,
) -> tuple[int, int, int, int] | None:
    """Inclusive-exclusive box of pixels close to rgb, or None.

    region, when set, is (left, top, right, bottom) and clips the search.
    Pixels with alpha below 128 are ignored.
    """
    if region is None:
        x0, y0, x1, y1 = 0, 0, width, height
    else:
        x0 = max(0, region[0])
        y0 = max(0, region[1])
        x1 = min(width, region[2])
        y1 = min(height, region[3])
    want_r, want_g, want_b = rgb
    left = top = right = bottom = None
    for y in range(y0, y1):
        row = y * width
        for x in range(x0, x1):
            i = (row + x) * 4
            if rgba[i + 3] < 128:
                continue
            if (
                abs(rgba[i] - want_r) <= tolerance
                and abs(rgba[i + 1] - want_g) <= tolerance
                and abs(rgba[i + 2] - want_b) <= tolerance
            ):
                if left is None:
                    left = right = x
                    top = bottom = y
                else:
                    left = min(left, x)
                    right = max(right, x)
                    top = min(top, y)
                    bottom = max(bottom, y)
    if left is None:
        return None
    return left, top, right + 1, bottom + 1


def _chunk(tag: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)


def encode_rgba_for_test(width: int, height: int, rgba: bytes, filter_type: int = 0) -> bytes:
    """Build a non-interlaced RGBA8 PNG. Used only by the decoder self-check."""
    stride = width * 4
    raw = bytearray()
    prior = bytearray(stride)
    for y in range(height):
        row = bytearray(rgba[y * stride : (y + 1) * stride])
        stored = bytearray(stride)
        if filter_type == 0:
            stored[:] = row
        elif filter_type == 1:
            for i, value in enumerate(row):
                left = row[i - 4] if i >= 4 else 0
                stored[i] = (value - left) & 0xFF
        elif filter_type == 2:
            for i, value in enumerate(row):
                stored[i] = (value - prior[i]) & 0xFF
        elif filter_type == 3:
            for i, value in enumerate(row):
                left = row[i - 4] if i >= 4 else 0
                stored[i] = (value - ((left + prior[i]) // 2)) & 0xFF
        elif filter_type == 4:
            for i, value in enumerate(row):
                left = row[i - 4] if i >= 4 else 0
                up = prior[i]
                up_left = prior[i - 4] if i >= 4 else 0
                stored[i] = (value - _paeth(left, up, up_left)) & 0xFF
        else:
            raise PngError("test encoder filter must be 0, 1, 2, 3, or 4")
        raw.append(filter_type)
        raw += stored
        prior = row
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return _SIGNATURE + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(bytes(raw))) + _chunk(b"IEND", b"")


def self_check() -> None:
    pixels = bytes(
        [
            255, 0, 255, 255,
            253, 251, 247, 255,
            26, 31, 36, 255,
            255, 0, 255, 128,
        ]
    )
    for filt in (0, 1, 2, 3, 4):
        encoded = encode_rgba_for_test(2, 2, pixels, filt)
        width, height, rgba = decode_png(encoded)
        if (width, height, rgba) != (2, 2, pixels):
            raise SystemExit(f"png_probe self-check failed for filter {filt}")
    box = find_color_bbox(2, 2, pixels, (255, 0, 255), 0)
    # Alpha 128 is kept. Magenta sits at (0, 0) and (1, 1).
    if box != (0, 0, 2, 2):
        raise SystemExit(f"png_probe bbox self-check failed: {box}")
    if find_color_bbox(2, 2, pixels, (255, 0, 255), 0, region=(1, 0, 2, 1)) is not None:
        raise SystemExit("png_probe region self-check failed")


if __name__ == "__main__":
    self_check()
    print("ok   png_probe")
