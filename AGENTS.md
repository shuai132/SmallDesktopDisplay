# Repository Guidelines

## Project Structure & Module Organization

This PlatformIO Arduino project targets the ESP8266 NodeMCU (`esp12e`). The main firmware is in `src/SmallDesktopDisplay.cpp`; feature modules live under `src/Animate/`, `src/Network/`, `src/ImageViewer/`, `src/WebController/`, and `src/weatherNum/`. Shared configuration is in `src/config.h`, while display setup belongs in `include/TFT_User_Setup.h`. Browser assets are under `web/`, and `tools/image_push.py` is the host-side RGB565 uploader. Fonts, icons, and animation frames are generated headers under `src/font/`, `src/img/`, and module-specific `img/` directories. The `test/` tree contains experimental sketches and early PlatformIO test material; Python protocol tests live under `tools/`.

## Build, Test, and Development Commands

Run commands from the repository root:

- `pio run -e esp12e` — install pinned dependencies and compile the firmware.
- `pio run -t upload -e esp12e` — upload over OTA to `SmallDesktopDisplay.local`; the device must already support OTA and share the network.
- `pio run -t upload -e esp12e_serial` — upload over an auto-detected USB serial port; pass `--upload-port` when more than one candidate exists.
- `pio device monitor` — open the serial monitor for boot and diagnostic output.
- `pio test -e esp12e` — run PlatformIO-compatible tests when adding or modifying tests; hardware-facing behavior still requires device validation.
- `python3 -m unittest tools/test_image_push.py` — verify CLI, RGB565 and browser protocol parity.
- `pio run -t clean` — remove generated build output before a clean rebuild.

After every completed modification, automatically run `pio run -t upload -e esp12e` to build and upload the firmware to the configured device. Report upload failures explicitly; do not silently treat a successful compile as a successful device update.

Never commit `.pio/` or other generated build artifacts.

## Coding Style & Naming Conventions

Follow the existing Arduino C++ style: two-space indentation in production code, braces on the next line for functions and types, and one declaration per line. Use `PascalCase` for classes, `camelCase` for functions, and `UPPER_SNAKE_CASE` for compile-time macros. Keep hardware constants and feature flags centralized in `src/config.h`. Preserve concise comments where hardware behavior is non-obvious; avoid reformatting unrelated legacy code or generated asset headers.

## Testing Guidelines

Name new focused tests `test_<feature>.cpp` and keep fixtures close to the tested module. At minimum, require a successful `pio run`. For display, Wi-Fi, OTA, button, or sensor changes, verify on an ESP8266 device and record the board, display orientation, and observed result. Include screenshots or serial logs when visual or timing behavior changes.

## Firmware Size Tracking

Before every commit, run `pio run` and require a successful build. Replace the contents of `FIRMWARE_SIZE.txt` with only the exact `RAM:` and `Flash:` summary lines printed by that build, and include the file in the same commit. This file records only the current firmware size, not its history. Never estimate or reuse size values from an earlier build.

## Commit & Pull Request Guidelines

Recent commits use concise conventional prefixes such as `feat:`, `fix:`, `build:`, and `chore:`; follow that pattern and keep each commit reviewable. Pull requests should explain the behavior change, list validation commands and hardware checks, link relevant issues, and include before/after images for UI changes. Call out changes to pins, dependencies, network behavior, or persistent EEPROM data.

## Security & Configuration

Do not commit real Wi-Fi credentials, location identifiers, host-specific paths, or device secrets. Keep example values in source and document any required local configuration in the pull request.
