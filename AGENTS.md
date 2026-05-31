# Repository Guidelines

## Project Structure & Module Organization
Core firmware lives under `project/src/`; public headers are in `project/inc/`. The Humplog protocol and runtime state machine are implemented in `project/src/humplog.c`, with storage abstraction in `project/src/humplog_fs.c`, SD SPI transport in `project/src/sd_spi.c`, and board support in `wk_*.c`. Vendor MCU and peripheral code is kept under `libraries/`. FatFs is vendored as a git submodule in `third_party/FatFs/`. Host-side unit tests live in `tests/`, currently centered on `tests/humplog_fs_test.c`.

## Build, Test, and Development Commands
- `make -B`: rebuild the full AT32 firmware and generate `build/humplog.elf`, `.hex`, and `.bin`.
- `make host-test`: build and run the host-native RAM-disk tests for `humplog_fs`.
- `make clean`: remove `build/` artifacts.

Run commands from the repository root. The cross build expects `arm-none-eabi-gcc` in `PATH` or `GCC_PATH` set explicitly.

## Coding Style & Naming Conventions
Use C11-compatible, ASCII-only source. Follow the existing style: 2-space indentation, K&R braces, and short static helpers near their call sites. Prefix subsystem functions and globals consistently, for example `humplog_*`, `sd_spi_*`, `wk_usart1_*`. Keep headers minimal and place exported declarations in `project/inc/`. Use `uint8_t`, `uint16_t`, and `uint32_t` for protocol and hardware-facing code.

## Testing Guidelines
Add or update host tests when changing `humplog_fs` behavior or parser-visible file semantics. Keep new tests in `tests/` and prefer focused functions such as `test_iterate_dir()` or `test_write_append_and_sparse()`. Before submitting changes, run both `make host-test` and `make -B`; protocol-only changes still need a full firmware compile check.

## Commit & Pull Request Guidelines
Recent history uses short imperative commit subjects, e.g. `Improve Humplog logging throughput` or `Add SPI1 SD card backend with FatFs`. Keep subjects concise and capitalized. For pull requests, include:
- a short summary of user-visible behavior changes,
- the exact verification commands you ran,
- any hardware assumptions or untested SD-card/baud-rate scenarios.

## Hardware & Configuration Notes
The target is an `AT32F421x8` with `64KB flash` and `16KB RAM`; be conservative with buffer growth. SPI1 is used for SD-card access, and `config.txt` compatibility matters for legacy Humplog hosts.
