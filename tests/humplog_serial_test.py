#!/usr/bin/env python3
#
# HumpLog - OpenLog-compatible serial logger firmware for AT32F421
# Copyright (C) 2025  HumpbackLab
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
"""Humplog serial test tool for the AT32 firmware.

This script uses only the Python standard library and is intended for Linux
hosts with a USB-to-TTL adapter exposed as /dev/ttyUSB*.
"""

from __future__ import annotations

import argparse
import os
import re
import select
import sys
import termios
import time
from dataclasses import dataclass


PROMPT = b"12>"
ESCAPE = b"\x1a\x1a\x1a"
BAUD_MAP = {
    300: termios.B300,
    600: termios.B600,
    1200: termios.B1200,
    2400: termios.B2400,
    4800: termios.B4800,
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
    460800: termios.B460800,
    500000: termios.B500000,
    576000: termios.B576000,
    921600: termios.B921600,
    1000000: termios.B1000000,
}


class TestFailure(RuntimeError):
    pass


@dataclass
class StressResult:
    total_bytes: int
    elapsed_s: float
    bytes_per_s: float


class HumplogPort:
    def __init__(self, device: str, baud: int, timeout: float) -> None:
        if baud not in BAUD_MAP:
            raise ValueError(f"unsupported baud: {baud}")

        self.device = device
        self.baud = baud
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.timeout = timeout
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0
        attrs[1] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0
        attrs[4] = BAUD_MAP[baud]
        attrs[5] = BAUD_MAP[baud]
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def close(self) -> None:
        os.close(self.fd)
        self.fd = -1

    def reopen(self, baud: int) -> None:
        if self.fd >= 0:
            self.close()
        self.__init__(self.device, baud, self.timeout)

    def write(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            _, writable, _ = select.select([], [self.fd], [], self.timeout)
            if not writable:
                raise TimeoutError("serial write timeout")
            written = os.write(self.fd, view)
            view = view[written:]

    def read_until_idle(self, idle_s: float, overall_s: float | None = None) -> bytes:
        deadline = time.monotonic() + (overall_s if overall_s is not None else self.timeout)
        last_data = time.monotonic()
        chunks: list[bytes] = []

        while True:
            now = time.monotonic()
            if chunks and (now - last_data) >= idle_s:
                break
            if now >= deadline:
                break

            timeout = min(0.05, deadline - now)
            readable, _, _ = select.select([self.fd], [], [], timeout)
            if not readable:
                continue
            chunk = os.read(self.fd, 4096)
            if chunk:
                chunks.append(chunk)
                last_data = time.monotonic()

        return b"".join(chunks)

    def read_until_prompt(self, overall_s: float | None = None) -> bytes:
        deadline = time.monotonic() + (overall_s if overall_s is not None else self.timeout)
        data = bytearray()

        while time.monotonic() < deadline:
            readable, _, _ = select.select([self.fd], [], [], 0.05)
            if not readable:
                continue
            chunk = os.read(self.fd, 4096)
            if chunk:
                data.extend(chunk)
                if PROMPT in data:
                    return bytes(data)

        raise TimeoutError("prompt not received")

    def sync_command_mode(self) -> bytes:
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        # This works in both states:
        # - append/newlog mode: ESCAPE exits to command mode, then '?' prints help
        # - command mode: ESCAPE becomes command text, then '?' yields an error and prompt
        self.write(ESCAPE + b"?\r")
        return self.read_until_idle(idle_s=0.3, overall_s=max(2.5, self.timeout))

    def command(self, text: str) -> bytes:
        payload = text.encode("ascii") + b"\r"
        self.write(payload)
        try:
            return self.read_until_prompt(overall_s=max(2.0, self.timeout))
        except TimeoutError:
            self.sync_command_mode()
            self.write(payload)
            return self.read_until_prompt(overall_s=max(2.0, self.timeout))

    def mode_command(self, text: str) -> bytes:
        self.write(text.encode("ascii") + b"\r")
        return self.read_until_idle(idle_s=0.2, overall_s=max(1.0, self.timeout))


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise TestFailure(message)


def decode_text(data: bytes) -> str:
    return data.decode("utf-8", errors="replace")


def parse_size(response: bytes) -> int:
    text = decode_text(response)
    match = re.search(r"\r\n(\d+)\r\n12>", text)
    if not match:
        raise TestFailure(f"unable to parse size from response: {text!r}")
    return int(match.group(1))


def cleanup_file(port: HumplogPort, name: str) -> None:
    port.command(f"rm {name}")


def cleanup_dir(port: HumplogPort, name: str) -> None:
    port.command(f"rm -rf {name}")


def switch_baud(port: HumplogPort, baud: int, settle_s: float = 0.5) -> bytes:
    if baud not in BAUD_MAP:
        raise ValueError(f"unsupported baud: {baud}")

    port.write(f"baud {baud}\r".encode("ascii"))
    pre_switch = port.read_until_idle(idle_s=0.2, overall_s=max(1.0, port.timeout))
    time.sleep(settle_s)
    port.reopen(baud)
    sync_response = port.sync_command_mode()
    expect(PROMPT in sync_response, f"failed to synchronize after switching to {baud}")
    return pre_switch + sync_response


def test_basic_commands(port: HumplogPort) -> None:
    disk_response = port.command("disk")
    expect(b"MID:" in disk_response or b"disk info unavailable" in disk_response,
           "disk command returned unexpected output")

    ls_response = port.command("ls")
    expect(PROMPT in ls_response, "ls did not return a prompt")


def test_append_and_read(port: HumplogPort) -> None:
    filename = "BASIC.TXT"
    payload = b"hello-humplog\nsecond-line\n"

    cleanup_file(port, filename)
    port.mode_command(f"append {filename}")
    port.write(payload + ESCAPE)
    exit_response = port.read_until_prompt(overall_s=max(2.0, port.timeout))
    expect(PROMPT in exit_response, "append mode did not exit to command prompt")

    size_response = port.command(f"size {filename}")
    expect(parse_size(size_response) == len(payload), "append size mismatch")

    read_response = port.command(f"read {filename} 0 {len(payload)}")
    expect(payload in read_response, "append readback mismatch")


def test_write_mode(port: HumplogPort) -> None:
    filename = "WRITE.TXT"
    expected = b"alpha\nbeta\n"

    cleanup_file(port, filename)
    port.mode_command(f"write {filename} 0")
    port.write(b"alpha\r\nbeta\r\n\r\n")
    write_exit = port.read_until_prompt(overall_s=max(2.0, port.timeout))
    expect(PROMPT in write_exit, "write mode did not exit to prompt")

    size_response = port.command(f"size {filename}")
    expect(parse_size(size_response) == len(expected), "write mode size mismatch")

    read_response = port.command(f"read {filename} 0 {len(expected)}")
    expect(expected in read_response, "write mode readback mismatch")


def test_directory_ops(port: HumplogPort) -> None:
    dirname = "TDIR"

    cleanup_dir(port, dirname)
    port.command(f"md {dirname}")
    cd_response = port.command(f"cd {dirname}")
    expect(PROMPT in cd_response, "cd into directory failed")

    pwd_response = port.command("cd")
    expect(b"\\TDIR" in pwd_response, "current path did not match test directory")

    port.command("cd ..")
    port.command(f"rm -rf {dirname}")
    verify = port.command(f"ls {dirname}")
    expect(b"<empty>" in verify or dirname.encode("ascii") not in verify,
           "directory was not removed")


def run_stress_test(port: HumplogPort, total_bytes: int, chunk_bytes: int) -> StressResult:
    filename = "STRESS.BIN"
    chunk = (b"0123456789ABCDEF" * ((chunk_bytes + 15) // 16))[:chunk_bytes]

    cleanup_file(port, filename)
    port.mode_command(f"append {filename}")

    remaining = total_bytes
    start = time.perf_counter()
    while remaining > 0:
        current = chunk if remaining >= len(chunk) else chunk[:remaining]
        port.write(current)
        remaining -= len(current)
    port.write(ESCAPE)
    exit_response = port.read_until_idle(
        idle_s=0.5,
        overall_s=max(15.0, port.timeout + (total_bytes / 2048.0))
    )
    expect(PROMPT in exit_response, "stress append did not exit to prompt")
    elapsed = time.perf_counter() - start

    size_response = port.command(f"size {filename}")
    expect(parse_size(size_response) == total_bytes, "stress file size mismatch")
    return StressResult(total_bytes=total_bytes,
                        elapsed_s=elapsed,
                        bytes_per_s=(total_bytes / elapsed) if elapsed > 0.0 else 0.0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Humplog serial protocol test tool")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="serial device path")
    parser.add_argument("--baud", type=int, default=9600, help="serial baud rate")
    parser.add_argument("--timeout", type=float, default=3.0, help="serial timeout in seconds")
    parser.add_argument("--skip-basic", action="store_true", help="skip basic command tests")
    parser.add_argument("--skip-stress", action="store_true", help="skip stress test")
    parser.add_argument("--stress-bytes", type=int, default=64 * 1024,
                        help="stress payload size in bytes")
    parser.add_argument("--stress-chunk", type=int, default=256,
                        help="single write chunk size in bytes")
    args = parser.parse_args()

    port = HumplogPort(args.port, args.baud, args.timeout)
    try:
        print(f"[INFO] probing {args.port} @ {args.baud} baud")
        sync_response = port.sync_command_mode()
        expect(PROMPT in sync_response, "failed to synchronize command mode")

        if not args.skip_basic:
            print("[INFO] running basic command tests")
            print("[INFO] step: command sync/help/disk/ls")
            test_basic_commands(port)
            print("[INFO] step: append/read")
            test_append_and_read(port)
            print("[INFO] step: write mode")
            test_write_mode(port)
            print("[INFO] step: directory ops")
            test_directory_ops(port)
            print("[PASS] basic tests passed")

        if not args.skip_stress:
            print(f"[INFO] running stress test: {args.stress_bytes} bytes")
            result = run_stress_test(port, args.stress_bytes, args.stress_chunk)
            print(f"[PASS] stress test passed: {result.total_bytes} bytes in "
                  f"{result.elapsed_s:.3f}s ({result.bytes_per_s:.1f} B/s)")

        return 0
    except (OSError, TimeoutError, TestFailure, ValueError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
