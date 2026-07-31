"""Compress the device web controller into a generated PROGMEM C++ header."""

import gzip
from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_header = generated_dir / "web_assets.generated.hpp"
assets = (
    ("kWebIndexGzip", project_dir / "web" / "index.html", "text/html; charset=utf-8"),
    ("kWebCssGzip", project_dir / "web" / "app.css", "text/css; charset=utf-8"),
    ("kWebProtocolJsGzip", project_dir / "web" / "protocol.js", "text/javascript; charset=utf-8"),
    ("kWebJsGzip", project_dir / "web" / "app.js", "text/javascript; charset=utf-8"),
)


def byte_array(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 20):
        rows.append(", ".join(f"0x{value:02x}" for value in data[offset : offset + 20]))
    return ",\n    ".join(rows)


generated_dir.mkdir(parents=True, exist_ok=True)
parts = ["#pragma once", "", "#include <Arduino.h>", ""]
for symbol, source, content_type in assets:
    compressed = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)
    parts.extend(
        (
            f"static const uint8_t {symbol}[] PROGMEM = {{",
            f"    {byte_array(compressed)}",
            "};",
            f"static constexpr size_t {symbol}Size = sizeof({symbol});",
            f'static constexpr const char* {symbol}ContentType = "{content_type}";',
            "",
        )
    )

content = "\n".join(parts)
if not generated_header.exists() or generated_header.read_text(encoding="utf-8") != content:
    generated_header.write_text(content, encoding="utf-8")

env.Append(CPPPATH=[str(generated_dir)])
