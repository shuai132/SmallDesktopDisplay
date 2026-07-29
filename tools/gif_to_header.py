#!/usr/bin/env python3
"""Generate and analyze display-ready frames from the Totoro GIF."""

import argparse
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools" / "longmao.gif"
OUTPUT = ROOT / "src" / "Animate" / "img" / "longmao.h"
FRAME_STEP = 2
FRAME_SIZE = (240, 240)
JPEG_QUALITY = 55
TILE_SIZE = 16
MAXIMUM_PATCHES = 4


@dataclass(frozen=True)
class Rect:
    x: int
    y: int
    width: int
    height: int


CLOCK_RECTS = (
    Rect(20, 82, 36, 90),
    Rect(60, 82, 36, 90),
    Rect(101, 82, 36, 90),
    Rect(141, 82, 36, 90),
    Rect(182, 112, 18, 30),
    Rect(202, 112, 18, 30),
)


@dataclass(frozen=True)
class EncodedFrame:
    image: Image.Image
    jpeg: bytes
    pixels: tuple[int, ...]
    duration_ms: int


def format_bytes(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        rows.append("  " + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    return "\n".join(rows)


def rgb565_pixels(image: Image.Image) -> tuple[int, ...]:
    return tuple(
        ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        for red, green, blue in image.convert("RGB").get_flattened_data()
    )


def encode_jpeg(image: Image.Image) -> bytes:
    encoded = BytesIO()
    image.save(
        encoded,
        "JPEG",
        quality=JPEG_QUALITY,
        optimize=True,
        progressive=False,
        subsampling=2,
    )
    return encoded.getvalue()


def load_frames() -> list[EncodedFrame]:
    image = Image.open(SOURCE)
    frames = []

    for index in range(0, image.n_frames, FRAME_STEP):
        duration_ms = 0
        for source_index in range(index, min(index + FRAME_STEP, image.n_frames)):
            image.seek(source_index)
            duration_ms += image.info.get("duration", 40)

        image.seek(index)
        source_frame = image.convert("RGB").resize(FRAME_SIZE, Image.Resampling.LANCZOS)
        jpeg = encode_jpeg(source_frame)
        decoded = Image.open(BytesIO(jpeg)).convert("RGB")
        frames.append(EncodedFrame(source_frame, jpeg, rgb565_pixels(decoded), duration_ms))

    return frames


def dirty_tiles(
    previous: tuple[int, ...],
    current: tuple[int, ...],
    forced_rects: tuple[Rect, ...] = (),
) -> list[list[bool]]:
    width, height = FRAME_SIZE
    tile_columns = (width + TILE_SIZE - 1) // TILE_SIZE
    tile_rows = (height + TILE_SIZE - 1) // TILE_SIZE
    dirty = [[False] * tile_columns for _ in range(tile_rows)]

    for tile_y in range(tile_rows):
        y_end = min((tile_y + 1) * TILE_SIZE, height)
        for tile_x in range(tile_columns):
            x_start = tile_x * TILE_SIZE
            x_end = min((tile_x + 1) * TILE_SIZE, width)
            for y in range(tile_y * TILE_SIZE, y_end):
                row = y * width
                if previous[row + x_start : row + x_end] != current[row + x_start : row + x_end]:
                    dirty[tile_y][tile_x] = True
                    break

    for rect in forced_rects:
        first_tile_x = rect.x // TILE_SIZE
        last_tile_x = (rect.x + rect.width - 1) // TILE_SIZE
        first_tile_y = rect.y // TILE_SIZE
        last_tile_y = (rect.y + rect.height - 1) // TILE_SIZE
        for tile_y in range(first_tile_y, last_tile_y + 1):
            for tile_x in range(first_tile_x, last_tile_x + 1):
                dirty[tile_y][tile_x] = True

    return dirty


def merge_dirty_tiles(dirty: list[list[bool]]) -> list[Rect]:
    """Merge equal horizontal tile runs vertically without adding clean tiles."""
    active: dict[tuple[int, int], Rect] = {}
    completed = []

    for tile_y, row in enumerate(dirty):
        runs = []
        tile_x = 0
        while tile_x < len(row):
            if not row[tile_x]:
                tile_x += 1
                continue
            run_start = tile_x
            while tile_x < len(row) and row[tile_x]:
                tile_x += 1
            runs.append((run_start, tile_x))

        next_active = {}
        for run in runs:
            if run in active:
                old = active.pop(run)
                next_active[run] = Rect(old.x, old.y, old.width, old.height + TILE_SIZE)
            else:
                next_active[run] = Rect(
                    run[0] * TILE_SIZE,
                    tile_y * TILE_SIZE,
                    (run[1] - run[0]) * TILE_SIZE,
                    TILE_SIZE,
                )
        completed.extend(active.values())
        active = next_active

    completed.extend(active.values())
    width, height = FRAME_SIZE
    return [
        Rect(rect.x, rect.y, min(rect.width, width - rect.x), min(rect.height, height - rect.y))
        for rect in completed
    ]


def combine_rectangles(rectangles: list[Rect], maximum_count: int) -> list[Rect]:
    """Greedily merge the pair that introduces the least extra transfer area."""
    combined = list(rectangles)
    while len(combined) > maximum_count:
        best_pair = None
        best_cost = None
        best_rect = None
        for first in range(len(combined) - 1):
            for second in range(first + 1, len(combined)):
                left = min(combined[first].x, combined[second].x)
                top = min(combined[first].y, combined[second].y)
                right = max(
                    combined[first].x + combined[first].width,
                    combined[second].x + combined[second].width,
                )
                bottom = max(
                    combined[first].y + combined[first].height,
                    combined[second].y + combined[second].height,
                )
                merged = Rect(left, top, right - left, bottom - top)
                cost = (
                    merged.width * merged.height
                    - combined[first].width * combined[first].height
                    - combined[second].width * combined[second].height
                )
                if best_cost is None or cost < best_cost:
                    best_pair = (first, second)
                    best_cost = cost
                    best_rect = merged

        first, second = best_pair
        combined[first] = best_rect
        del combined[second]

    return combined


def rect_pixels(pixels: tuple[int, ...], rect: Rect) -> list[int]:
    frame_width, _ = FRAME_SIZE
    values = []
    for y in range(rect.y, rect.y + rect.height):
        row = y * frame_width
        values.extend(pixels[row + rect.x : row + rect.x + rect.width])
    return values


def packbits565(pixels: list[int]) -> bytes:
    """Encode RGB565 pixels as literal and repeated runs.

    Control bytes 0..127 contain 1..128 literal pixels. Control bytes
    128..255 contain 2..129 copies of the following pixel.
    """
    encoded = bytearray()
    index = 0

    while index < len(pixels):
        repeat = 1
        while (
            index + repeat < len(pixels)
            and pixels[index + repeat] == pixels[index]
            and repeat < 129
        ):
            repeat += 1

        if repeat >= 3:
            encoded.append(0x80 | (repeat - 2))
            encoded.extend(pixels[index].to_bytes(2, "little"))
            index += repeat
            continue

        literal_start = index
        index += repeat
        while index < len(pixels) and index - literal_start < 128:
            next_repeat = 1
            while (
                index + next_repeat < len(pixels)
                and pixels[index + next_repeat] == pixels[index]
                and next_repeat < 3
            ):
                next_repeat += 1
            if next_repeat >= 3:
                break
            index += next_repeat

        literal_count = index - literal_start
        encoded.append(literal_count - 1)
        for pixel in pixels[literal_start:index]:
            encoded.extend(pixel.to_bytes(2, "little"))

    return bytes(encoded)


def analyze(frames: list[EncodedFrame]) -> None:
    full_jpeg_bytes = sum(len(frame.jpeg) for frame in frames)
    delta_bytes = len(frames[0].jpeg)
    jpeg_delta_bytes = len(frames[0].jpeg)
    total_area = FRAME_SIZE[0] * FRAME_SIZE[1]
    changed_areas = []
    patch_counts = []
    raw_patch_count = 0
    rle_patch_count = 0
    candidate_limits = (4, 6, 8, 12)
    candidate_bytes = {limit: len(frames[0].jpeg) for limit in candidate_limits}
    candidate_areas = {limit: 0 for limit in candidate_limits}

    print(
        "frame  patches  changed   area%   raw-bytes  encoded-bytes  encoding"
    )
    print(
        f"{0:5d}  {1:7d}  {total_area:7d}  {100.0:6.1f}  "
        f"{total_area * 2:9d}  {len(frames[0].jpeg):13d}  jpeg"
    )

    for index in range(1, len(frames)):
        dirty = dirty_tiles(
            frames[index - 1].pixels,
            frames[index].pixels,
            CLOCK_RECTS,
        )
        patches = merge_dirty_tiles(dirty)
        changed_area = sum(rect.width * rect.height for rect in patches)
        raw_bytes = changed_area * 2
        encoded_bytes = 0
        jpeg_patch_bytes = 0
        encodings = []

        for rect in patches:
            pixels = rect_pixels(frames[index].pixels, rect)
            rle = packbits565(pixels)
            jpeg_patch_bytes += len(
                encode_jpeg(
                    frames[index].image.crop(
                        (rect.x, rect.y, rect.x + rect.width, rect.y + rect.height)
                    )
                )
            )
            raw_size = len(pixels) * 2
            if len(rle) < raw_size:
                encoded_bytes += len(rle)
                rle_patch_count += 1
                encodings.append("rle")
            else:
                encoded_bytes += raw_size
                raw_patch_count += 1
                encodings.append("raw")

        for limit in candidate_limits:
            candidate_patches = combine_rectangles(patches, limit)
            candidate_areas[limit] += sum(
                rect.width * rect.height for rect in candidate_patches
            )
            candidate_bytes[limit] += sum(
                len(
                    encode_jpeg(
                        frames[index].image.crop(
                            (rect.x, rect.y, rect.x + rect.width, rect.y + rect.height)
                        )
                    )
                )
                for rect in candidate_patches
            )

        delta_bytes += encoded_bytes
        jpeg_delta_bytes += jpeg_patch_bytes
        changed_areas.append(changed_area)
        patch_counts.append(len(patches))
        encoding_summary = (
            "rle" if encodings and all(value == "rle" for value in encodings)
            else "raw" if encodings and all(value == "raw" for value in encodings)
            else "mixed"
        )
        print(
            f"{index:5d}  {len(patches):7d}  {changed_area:7d}  "
            f"{changed_area * 100 / total_area:6.1f}  {raw_bytes:9d}  "
            f"{encoded_bytes:13d}  {encoding_summary}"
        )

    descriptor_bytes = sum(patch_counts) * 16 + len(frames) * 8
    delta_bytes_with_metadata = delta_bytes + descriptor_bytes
    average_area = sum(changed_areas) / len(changed_areas)
    average_patches = sum(patch_counts) / len(patch_counts)

    print()
    print(f"Frames:                       {len(frames)}")
    print(f"Full JPEG payload:            {full_jpeg_bytes} bytes")
    print(f"Delta payload:                {delta_bytes} bytes")
    print(f"JPEG patch payload:           {jpeg_delta_bytes} bytes")
    print(f"Estimated delta + metadata:   {delta_bytes_with_metadata} bytes")
    print(f"Payload ratio:                {delta_bytes / full_jpeg_bytes:.2f}x")
    print(f"JPEG patch ratio:             {jpeg_delta_bytes / full_jpeg_bytes:.2f}x")
    print(f"Average changed area:         {average_area / total_area * 100:.1f}%")
    print(f"Maximum changed area:         {max(changed_areas) / total_area * 100:.1f}%")
    print(f"Average patches per frame:    {average_patches:.1f}")
    print(f"Maximum patches per frame:    {max(patch_counts)}")
    print(f"Raw/RLE patch selections:     {raw_patch_count}/{rle_patch_count}")
    print(
        "Average 40MHz SPI payload:   "
        f"{(sum(changed_areas) / len(changed_areas)) * 16 / 40_000_000 * 1000:.2f} ms"
    )
    print()
    print("JPEG merge candidates:")
    print("max-patches  payload-bytes  ratio   average-area  40MHz-SPI")
    for limit in candidate_limits:
        average_candidate_area = candidate_areas[limit] / (len(frames) - 1)
        print(
            f"{limit:11d}  {candidate_bytes[limit]:13d}  "
            f"{candidate_bytes[limit] / full_jpeg_bytes:5.2f}x  "
            f"{average_candidate_area / total_area * 100:11.1f}%  "
            f"{average_candidate_area * 16 / 40_000_000 * 1000:8.2f}ms"
        )


def write_full_header(frames: list[EncodedFrame]) -> None:
    lines = ["#pragma once", "", "#include <pgmspace.h>", ""]
    for index, frame in enumerate(frames):
        lines.extend(
            [
                f"const uint8_t longmaoFrame{index}[] PROGMEM = {{",
                format_bytes(frame.jpeg),
                "};",
                "",
            ]
        )

    frame_names = ", ".join(f"longmaoFrame{index}" for index in range(len(frames)))
    frame_sizes = ", ".join(str(len(frame.jpeg)) for frame in frames)
    frame_durations = ", ".join(str(frame.duration_ms) for frame in frames)
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


def write_delta_header(frames: list[EncodedFrame]) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <pgmspace.h>",
        "",
        "const uint8_t longmaoKeyframe[] PROGMEM = {",
        format_bytes(frames[0].jpeg),
        "};",
        "",
    ]
    patch_records = []
    frame_starts = []
    frame_counts = []

    for frame_index, frame in enumerate(frames):
        previous = frames[frame_index - 1]
        dirty = dirty_tiles(previous.pixels, frame.pixels, CLOCK_RECTS)
        patches = combine_rectangles(merge_dirty_tiles(dirty), MAXIMUM_PATCHES)
        frame_starts.append(len(patch_records))
        frame_counts.append(len(patches))

        for patch_index, rect in enumerate(patches):
            data = encode_jpeg(
                frame.image.crop(
                    (rect.x, rect.y, rect.x + rect.width, rect.y + rect.height)
                )
            )
            name = f"longmaoPatch{frame_index}_{patch_index}"
            lines.extend(
                [
                    f"const uint8_t {name}[] PROGMEM = {{",
                    format_bytes(data),
                    "};",
                    "",
                ]
            )
            patch_records.append((name, len(data), rect))

    patch_initializers = ",\n".join(
        f"  {{{name}, {size}, {rect.x}, {rect.y}}}"
        for name, size, rect in patch_records
    )
    frame_starts_text = ", ".join(str(value) for value in frame_starts)
    frame_counts_text = ", ".join(str(value) for value in frame_counts)
    frame_durations_text = ", ".join(str(frame.duration_ms) for frame in frames)
    lines.extend(
        [
            f"constexpr uint16_t LONGMAO_FRAME_COUNT = {len(frames)};",
            f"constexpr uint16_t LONGMAO_PATCH_COUNT = {len(patch_records)};",
            "",
            "const AnimationPatch longmaoPatches[LONGMAO_PATCH_COUNT] PROGMEM = {",
            patch_initializers,
            "};",
            "",
            "const uint16_t longmaoFramePatchStarts[LONGMAO_FRAME_COUNT] PROGMEM = "
            f"{{{frame_starts_text}}};",
            "const uint8_t longmaoFramePatchCounts[LONGMAO_FRAME_COUNT] PROGMEM = "
            f"{{{frame_counts_text}}};",
            "const uint16_t longmaoFrameDurations[LONGMAO_FRAME_COUNT] PROGMEM = "
            f"{{{frame_durations_text}}};",
            "",
        ]
    )
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--mode",
        choices=("analyze", "delta", "full"),
        default="full",
        help="analyze delta efficiency or generate a full/delta JPEG header",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    frames = load_frames()
    if args.mode == "analyze":
        analyze(frames)
    elif args.mode == "delta":
        write_delta_header(frames)
    else:
        write_full_header(frames)


if __name__ == "__main__":
    main()
