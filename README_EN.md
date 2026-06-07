# HumpLog

HumpLog is an OpenLog-compatible firmware project by HumpbackLab. It maintains compatibility with the SparkFun OpenLog serial interface for legacy devices, while logging data from UART to an SPI SD card with support for higher baud rates (up to 1 Mbaud) and higher throughput.

## Current Capabilities

- Compatible with common OpenLog commands: `new`, `append`, `write`, `rm`, `size`, `read`, `cat`, `ls`, `md`, `cd`, `sync`, `init`, `reset`, `disk`, `baud`, `set`, `verbose`, `echo`
- Supports three startup modes: `newlog`, `seqlog`, `command`
- Uses `config.txt` to store compatible configuration: `baud,escape,esc#,mode,verb,echo,ignoreRX`
- Uses `SPI1 + SD SPI + FatFs` as the storage backend
- Log mode features a streaming write buffer to reduce performance loss from byte-by-byte flushing
- Current maximum reliable recording rate on real hardware: ~`74 KB/s @ 1000000 baud` (`256 KB` continuous write verified)

## Directory Structure

- `project/src/` — main firmware source code
- `project/inc/` — project header files
- `tests/` — host-side tests, currently covering `humplog_fs`
- `third_party/FatFs/` — FatFs submodule
- `libraries/` — AT32 CMSIS and peripheral drivers

## Hardware Connections

The SD card currently uses `SPI1`:

- `PA5` — SCK
- `PA6` — MISO
- `PA7` — MOSI
- `PA4` — SD CS
- `PA9` — USART1 TX
- `PA10` — USART1 RX
- `PB0` — status LED (active low)

## LED Status Indicator

| Mode | Data Flow | LED Behavior |
|------|-----------|--------------|
| `newlog` / `append` | No data (idle >100 ms) | **Solid on** |
| `newlog` / `append` | Writing data | **Fast blink** (~20 Hz) |
| `command` / `write` | — | **Off** |
| `baud` / `set` menu | — | **Off** |

## config.txt Defaults

On first power-up, the firmware automatically creates `config.txt` in the SD card root directory with the following format:

```
115200,26,0,0,1,1,1
baud,escape,esc#,mode,verb,echo,ignoreRX
```

| Field | Default | Meaning |
|-------|---------|---------|
| `baud` | `115200` | Serial baud rate (300–1000000) |
| `escape` | `26` (0x1A) | Decimal value of the ESCAPE character |
| `esc#` | `0` | ESCAPE trigger count (0 = disable ESCAPE, set to 3 to enable) |
| `mode` | `0` | Startup mode: 0 = newlog, 1 = seqlog, 2 = command |
| `verb` | `1` | Verbose error messages: 0 = short (!), 1 = full |
| `echo` | `1` | Command echo: 0 = off, 1 = on |
| `ignoreRX` | `1` | Ignore RX noise at power-up: 0 = off, 1 = on |

Configuration can be changed online using the `set` command, or by directly editing `config.txt` and then running `init` to reload.
ESCAPE is disabled by default to prevent accidentally entering command mode while logging. If you need to run stress tests, set `esc#` to 3, otherwise command mode will be inaccessible.

## Build & Test

Run the following commands from the repository root:

```bash
make host-test
make -B
```

- `make host-test` — compiles and runs the `humplog_fs` host test using the native `gcc`
- `make -B` — performs a full rebuild of the firmware using `arm-none-eabi-gcc`, producing `build/humplog.elf/.hex/.bin`

## Compatibility Notes

The current filesystem path cache limit is `32 B`, so it is best to place log files directly in the root directory or use only shallow directory hierarchies. Deep directory paths may cause path cache failures.

The current filesystem supports a maximum of `64` nodes. Exceeding this limit will affect the output of `ls`, `new`, and other commands in command mode, but will not affect recording in newlog mode.

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**. See the [LICENSE](LICENSE) file for details.

The Humplog original source code (`project/src/`, `project/inc/`, and `tests/`) is covered by GPLv3.
AT32 CMSIS/driver code under `libraries/` is governed by the Artery Technology BSP license terms.
The `third_party/FatFs/` submodule follows its own license. If you use code from this project in a closed-source project, you must comply with the GPLv3 open source license.
