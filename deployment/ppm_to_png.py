#!/usr/bin/env python3
"""Convert pomelo_capture's binary RGB PPM to PNG, without image processing.

Usage: python deployment/ppm_to_png.py capture.ppm capture.png
Only the exact P6 / width height / 255 format written by pomelo_capture is needed.
Uses Python's standard library; preserves every RGB pixel.
"""
import struct
import sys
import zlib
from pathlib import Path


def chunk(kind: bytes, data: bytes) -> bytes:
    payload = kind + data
    return struct.pack('>I', len(data)) + payload + struct.pack('>I', zlib.crc32(payload))


def convert(source: Path, destination: Path) -> None:
    with source.open('rb') as stream:
        if stream.readline().strip() != b'P6':
            raise ValueError('Expected binary RGB PPM (P6)')
        width, height = map(int, stream.readline().split())
        if width <= 0 or height <= 0 or stream.readline().strip() != b'255':
            raise ValueError('Expected positive dimensions and 8-bit RGB')
        pixels = stream.read()
    stride = width * 3
    if len(pixels) != stride * height:
        raise ValueError('Unexpected PPM pixel count')
    rows = b''.join(b'\0' + pixels[row * stride:(row + 1) * stride] for row in range(height))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header)
    png += chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')
    destination.write_bytes(png)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('Usage: ppm_to_png.py input.ppm output.png')
    convert(Path(sys.argv[1]), Path(sys.argv[2]))
