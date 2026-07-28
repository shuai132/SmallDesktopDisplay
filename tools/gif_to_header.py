#!/usr/bin/env python3
"""Convert the Totoro GIF into full-screen JPEG frames for the ESP8266."""

from io import BytesIO
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools" / "longmao.gif"
OUTPUT = ROOT / "src" / "Animate" / "img" / "longmao.h"
FRAME_STEP = 2
FRAME_SIZE = (240, 240)
JPEG_QUALITY = 55


def format_bytes(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        rows.append("  " + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    return "\n".join(rows)


def main() -> None:
    image = Image.open(SOURCE)
    frames = []
    durations = []

    for index in range(0, image.n_frames, FRAME_STEP):
        image.seek(index)
        frame = image.convert("RGB").resize(FRAME_SIZE, Image.Resampling.LANCZOS)
        encoded = BytesIO()
        frame.save(
            encoded,
            "JPEG",
            quality=JPEG_QUALITY,
            optimize=True,
            progressive=False,
            subsampling=2,
        )
        frames.append(encoded.getvalue())
        durations.append(
            sum(
                image.info.get("duration", 40)
                for source_index in range(index, min(index + FRAME_STEP, image.n_frames))
                if not image.seek(source_index)
            )
        )

    lines = ["#pragma once", "", "#include <pgmspace.h>", ""]
    for index, frame in enumerate(frames):
        lines.extend(
            [
                f"const uint8_t longmaoFrame{index}[] PROGMEM = {{",
                format_bytes(frame),
                "};",
                "",
            ]
        )

    frame_names = ", ".join(f"longmaoFrame{index}" for index in range(len(frames)))
    frame_sizes = ", ".join(str(len(frame)) for frame in frames)
    frame_durations = ", ".join(str(duration) for duration in durations)
    lines.extend(
        [
            f"constexpr uint16_t LONGMAO_FRAME_COUNT = {len(frames)};",
            f"const uint8_t *const longmaoFrames[LONGMAO_FRAME_COUNT] PROGMEM = {{{frame_names}}};",
            f"const uint32_t longmaoFrameSizes[LONGMAO_FRAME_COUNT] PROGMEM = {{{frame_sizes}}};",
            f"const uint16_t longmaoFrameDurations[LONGMAO_FRAME_COUNT] PROGMEM = {{{frame_durations}}};",
            "",
        ]
    )
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
