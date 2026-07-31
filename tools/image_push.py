#!/usr/bin/env python3
"""Prepare and upload 240x240 RGB565 images to SmallDesktopDisplay."""

from __future__ import annotations

import argparse
import http.client
import json
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

try:
    from PIL import Image, ImageOps
except ImportError as exc:
    raise SystemExit("Pillow is required: python3 -m pip install -r tools/requirements-image.txt") from exc


WIDTH = 240
HEIGHT = 240
ROW_BYTES = WIDTH * 2
PAYLOAD_BYTES = WIDTH * HEIGHT * 2
HEADER_FORMAT = "<4sBBBBB3xHHII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAGIC = b"SDDI"
PROTOCOL_VERSION = 1
SLOT_FORMAT_VERSION = 1
COMMAND_STORE = 1
COMMAND_DISPLAY_ONLY = 2
PIXEL_FORMAT_RGB565_BE = 1
CURRENT_SLOT = 0xFF
SLOT_COUNT = 3
FEATURE_FRAME_RENDER = 1
DEFAULT_HOST = "SmallDesktopDisplay.local"
DEFAULT_PORT = 80

BAYER_8X8 = (
    (0, 48, 12, 60, 3, 51, 15, 63),
    (32, 16, 44, 28, 35, 19, 47, 31),
    (8, 56, 4, 52, 11, 59, 7, 55),
    (40, 24, 36, 20, 43, 27, 39, 23),
    (2, 50, 14, 62, 1, 49, 13, 61),
    (34, 18, 46, 30, 33, 17, 45, 29),
    (10, 58, 6, 54, 9, 57, 5, 53),
    (42, 26, 38, 22, 41, 25, 37, 21),
)


def log_event(started_at: float, message: str) -> None:
    print(f"[+{time.perf_counter() - started_at:.3f}s] {message}", file=sys.stderr, flush=True)


def positive_float(value: str) -> float:
    result = float(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return result


def fit_image(source: Image.Image, fit: str = "contain", scale: float = 1.0, rotate: int = 0) -> Image.Image:
    if fit not in {"contain", "cover", "stretch"}:
        raise ValueError(f"unknown fit mode: {fit}")
    if scale <= 0:
        raise ValueError("scale must be greater than zero")
    source = ImageOps.exif_transpose(source).convert("RGBA")
    if rotate:
        source = source.rotate(-rotate, expand=True)
    if fit == "stretch":
        base_width, base_height = WIDTH, HEIGHT
    else:
        ratio = (max if fit == "cover" else min)(WIDTH / source.width, HEIGHT / source.height)
        base_width = max(1, round(source.width * ratio))
        base_height = max(1, round(source.height * ratio))
    output_size = (max(1, round(base_width * scale)), max(1, round(base_height * scale)))
    transformed = source.resize(output_size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 255))
    canvas.alpha_composite(transformed, ((WIDTH - transformed.width) // 2, (HEIGHT - transformed.height) // 2))
    return canvas.convert("RGB")


def ordered_quantize(value: int, max_code: int, threshold: int) -> int:
    scaled = value * max_code
    code, remainder = divmod(scaled, 255)
    if code < max_code and remainder * 128 > (threshold * 2 + 1) * 255:
        code += 1
    return code


def rgb565_be_payload(image: Image.Image) -> bytes:
    if image.size != (WIDTH, HEIGHT) or image.mode != "RGB":
        raise ValueError("image must be RGB 240x240")
    payload = bytearray(PAYLOAD_BYTES)
    pixels = image.load()
    offset = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            red, green, blue = pixels[x, y]
            threshold = BAYER_8X8[y & 7][x & 7]
            pixel = (
                ordered_quantize(red, 31, threshold) << 11
                | ordered_quantize(green, 63, threshold) << 5
                | ordered_quantize(blue, 31, threshold)
            )
            payload[offset] = pixel >> 8
            payload[offset + 1] = pixel & 0xFF
            offset += 2
    return bytes(payload)


def build_bundle(
    source: Image.Image,
    slot: int | None = None,
    fit: str = "contain",
    scale: float = 1.0,
    rotate: int = 0,
    display_only: bool = False,
) -> bytes:
    if slot is not None and slot not in range(SLOT_COUNT):
        raise ValueError(f"slot must be between 0 and {SLOT_COUNT - 1}")
    payload = rgb565_be_payload(fit_image(source, fit=fit, scale=scale, rotate=rotate))
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        PROTOCOL_VERSION,
        COMMAND_DISPLAY_ONLY if display_only else COMMAND_STORE,
        PIXEL_FORMAT_RGB565_BE,
        HEADER_SIZE,
        CURRENT_SLOT if slot is None else slot,
        WIDTH,
        HEIGHT,
        len(payload),
        zlib.crc32(payload),
    )
    return header + payload


def load_bundle(path: Path, **options) -> bytes:
    with Image.open(path) as source:
        return build_bundle(source, **options)


def send_bundle(
    bundle: bytes,
    host: str,
    port: int,
    started_at: float | None = None,
    display_only: bool = False,
    render_mode: str = "progressive",
) -> None:
    if len(bundle) != HEADER_SIZE + PAYLOAD_BYTES:
        raise ValueError("invalid bundle size")
    if render_mode not in {"progressive", "frame"}:
        raise ValueError("render_mode must be progressive or frame")
    if "\r" in host or "\n" in host:
        raise ValueError("invalid host")
    if started_at is None:
        started_at = time.perf_counter()
    connection = socket.create_connection((host, port), timeout=10)
    try:
        connection.settimeout(20)
        wire_bundle = bytearray(bundle)
        wire_bundle[10] = FEATURE_FRAME_RENDER if render_mode == "frame" else 0
        host_header = f"[{host}]" if ":" in host and not host.startswith("[") else host
        if port != 80:
            host_header = f"{host_header}:{port}"
        headers = (
            "POST /api/images HTTP/1.1\r\n"
            f"Host: {host_header}\r\n"
            "Content-Type: application/octet-stream\r\n"
            f"Content-Length: {len(wire_bundle)}\r\n"
            "Connection: close\r\n\r\n"
        ).encode("ascii")
        connection.sendall(headers)
        connection.sendall(wire_bundle[:HEADER_SIZE])
        payload = memoryview(wire_bundle)[HEADER_SIZE:]
        send_started = time.perf_counter()
        if render_mode == "frame":
            connection.sendall(payload)
        else:
            for offset in range(0, len(payload), ROW_BYTES):
                connection.sendall(payload[offset : offset + ROW_BYTES])
        log_event(started_at, f"sent {len(payload)} RGB565 bytes in {time.perf_counter() - send_started:.3f}s")
        response = http.client.HTTPResponse(connection)
        response.begin()
        body = response.read()
        try:
            result = json.loads(body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise RuntimeError(f"HTTP {response.status}: invalid JSON response") from exc
        expected = "DISPLAYED" if display_only else "STORED"
        status = result.get("status") if isinstance(result, dict) else None
        ok = result.get("ok") if isinstance(result, dict) else None
        if response.status != 200 or ok is not True or status != expected:
            raise RuntimeError(f"HTTP {response.status}: {status or response.reason}")
        log_event(started_at, f"device returned {status}")
    finally:
        connection.close()


def command_image(args: argparse.Namespace) -> None:
    started_at = time.perf_counter()
    if args.display_only and args.slot is not None:
        raise ValueError("--display-only cannot be used with --slot")
    if args.display_only and args.no_send:
        raise ValueError("--display-only cannot be used with --no-send")
    bundle = load_bundle(
        args.image,
        slot=args.slot,
        fit=args.fit,
        scale=args.scale,
        rotate=args.rotate,
        display_only=args.display_only,
    )
    log_event(started_at, f"prepared SDDI v{PROTOCOL_VERSION} bundle ({len(bundle)} bytes)")
    if args.no_send:
        output = args.image.with_suffix(".sddimg")
        output.write_bytes(bundle)
        print(f"wrote {output}")
        return
    send_bundle(bundle, args.host, args.port, started_at, args.display_only, args.render_mode)
    action = "displayed without saving" if args.display_only else "displayed and stored"
    print(f"{action} on {args.host}:{args.port}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="source image file")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"device hostname or IP (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"device HTTP port (default: {DEFAULT_PORT})")
    parser.add_argument("--slot", type=int, choices=range(SLOT_COUNT), help="target A/B/C slot as 0/1/2 (default: current)")
    parser.add_argument("--display-only", action="store_true", help="display without saving to LittleFS")
    parser.add_argument("--no-send", action="store_true", help="write a .sddimg bundle without uploading")
    parser.add_argument("--render-mode", choices=("progressive", "frame"), default="progressive", help="progressive rows or verify before refresh")
    parser.add_argument("--rotate", type=int, choices=(0, 90, 180, 270), default=0, help="clockwise rotation after EXIF orientation")
    parser.add_argument("--fit", choices=("contain", "cover", "stretch"), default="contain", help="image fit mode")
    parser.add_argument("--scale", type=positive_float, default=1.0, help="additional centered scale")
    parser.set_defaults(handler=command_image)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        args.handler(args)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
