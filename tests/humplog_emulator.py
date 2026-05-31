#!/usr/bin/env python3
"""Minimal Humplog serial emulator for flight-controller integration testing.

This tool opens a real serial port, responds to a practical subset of the
Humplog command protocol, and logs everything the flight controller sends.
Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import signal
import sys
import termios
import time
from dataclasses import dataclass
from pathlib import Path


PROMPT_RECORD = b"12<"
PROMPT_COMMAND = b"12>"
ESCAPE_CHAR = 0x1A
ESCAPE_COUNT = 3
DEFAULT_BAUD = 115200
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


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime())


def sanitize_name(name: str) -> str:
    return name.strip().upper()


def is_printable_log_byte(value: int) -> bool:
    return value in (0x0A, 0x0D, 0x09) or 32 <= value <= 126


class SerialPort:
    def __init__(self, device: str, baud: int) -> None:
        if baud not in BAUD_MAP:
            raise ValueError(f"unsupported baud: {baud}")
        self.device = device
        self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self._configure(baud)

    def _configure(self, baud: int) -> None:
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
        self.baud = baud

    def set_baud(self, baud: int) -> None:
        if baud not in BAUD_MAP:
            raise ValueError(f"unsupported baud: {baud}")
        self._configure(baud)

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def read(self, max_bytes: int = 4096) -> bytes:
        readable, _, _ = select.select([self.fd], [], [], 0.1)
        if not readable:
            return b""
        try:
            return os.read(self.fd, max_bytes)
        except BlockingIOError:
            return b""

    def write(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            _, writable, _ = select.select([], [self.fd], [], 1.0)
            if not writable:
                raise TimeoutError("serial write timeout")
            written = os.write(self.fd, view)
            view = view[written:]


class SessionLogger:
    def __init__(self, output_dir: Path) -> None:
        self.output_dir = output_dir
        output_dir.mkdir(parents=True, exist_ok=True)
        self.event_log = output_dir / "events.log"
        self.jsonl_log = output_dir / "events.jsonl"
        self.rx_raw = open(output_dir / "rx.raw", "ab")
        self.tx_raw = open(output_dir / "tx.raw", "ab")

    def close(self) -> None:
        self.rx_raw.close()
        self.tx_raw.close()

    def raw_rx(self, data: bytes) -> None:
        self.rx_raw.write(data)
        self.rx_raw.flush()

    def raw_tx(self, data: bytes) -> None:
        self.tx_raw.write(data)
        self.tx_raw.flush()

    def event(self, direction: str, kind: str, payload: str, extra: dict | None = None) -> None:
        record = {
            "ts": now_iso(),
            "dir": direction,
            "kind": kind,
            "payload": payload,
        }
        if extra:
            record.update(extra)
        with self.event_log.open("a", encoding="utf-8") as handle:
            handle.write(f"[{record['ts']}] {direction} {kind}: {payload}\n")
        with self.jsonl_log.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, ensure_ascii=False) + "\n")

    def write_summary(self) -> None:
        rx_path = self.output_dir / "rx.raw"
        summary_path = self.output_dir / "summary.txt"
        data = rx_path.read_bytes() if rx_path.exists() else b""
        escape_triplet = bytes([ESCAPE_CHAR, ESCAPE_CHAR, ESCAPE_CHAR])
        first_binary_offset = next((i for i, value in enumerate(data) if not is_printable_log_byte(value)), -1)
        head_bytes = data if first_binary_offset < 0 else data[:first_binary_offset]
        head_text = head_bytes.decode("latin1", errors="replace")
        head_lines = [line.strip() for line in head_text.replace("\r", "\n").split("\n") if line.strip()]
        end_marker = b"E End of log"
        end_offset = data.find(end_marker)

        with summary_path.open("w", encoding="utf-8") as handle:
            handle.write(f"timestamp: {now_iso()}\n")
            handle.write(f"total_rx_bytes: {len(data)}\n")
            handle.write(f"text_header_bytes: {len(head_bytes)}\n")
            handle.write(f"binary_start_offset: {first_binary_offset}\n")
            handle.write(f"contains_escape_triplet: {escape_triplet in data}\n")
            handle.write(f"contains_end_of_log: {end_offset >= 0}\n")
            handle.write(f"end_of_log_offset: {end_offset}\n")
            handle.write(f"header_line_count: {len(head_lines)}\n")
            handle.write("\n[header_lines]\n")
            for line in head_lines:
                handle.write(line + "\n")


@dataclass
class EmulatorState:
    mode: str
    current_file: str | None
    current_dir: str
    echo_enabled: bool
    verbose_errors: bool
    escape_count: int
    command_buffer: bytearray
    write_buffer: bytearray
    pending_baud: int | None


class HumplogEmulator:
    def __init__(self, port: SerialPort, logger: SessionLogger, boot_mode: str) -> None:
        self.port = port
        self.logger = logger
        self.running = True
        self.state = EmulatorState(
            mode=boot_mode,
            current_file="LOG00000.TXT" if boot_mode == "record" else None,
            current_dir="/",
            echo_enabled=True,
            verbose_errors=True,
            escape_count=0,
            command_buffer=bytearray(),
            write_buffer=bytearray(),
            pending_baud=None,
        )
        self.files: dict[str, bytearray] = {}
        if self.state.current_file is not None:
            self.files[self.state.current_file] = bytearray()
        self.directories = {"/"}

    def stop(self, *_args: object) -> None:
        self.running = False

    def prompt(self) -> bytes:
        return PROMPT_RECORD if self.state.mode == "record" else PROMPT_COMMAND

    def send(self, data: bytes, kind: str = "response") -> None:
        if not data:
            return
        self.port.write(data)
        self.logger.raw_tx(data)
        self.logger.event("TX", kind, data.decode("latin1", errors="replace"))

    def send_text(self, text: str, kind: str = "response") -> None:
        self.send(text.encode("latin1"), kind=kind)

    def write_prompt(self) -> None:
        self.send(self.prompt(), kind="prompt")

    def report_error(self, message: str) -> None:
        if self.state.verbose_errors:
            self.send_text("\r\n" + message)
        else:
            self.send_text("\r\n!")

    def switch_to_command_mode(self) -> None:
        self.state.mode = "command"
        self.state.current_file = None
        self.state.escape_count = 0
        self.send_text("\r\n")
        self.write_prompt()

    def boot_banner(self) -> None:
        self.write_prompt()

    def normalize_path(self, raw_name: str) -> str:
        name = sanitize_name(raw_name)
        if not name or name == "/":
            return "/"
        if name.startswith("/"):
            return name
        if self.state.current_dir == "/":
            return "/" + name
        return self.state.current_dir.rstrip("/") + "/" + name

    def file_key(self, raw_name: str) -> str:
        path = self.normalize_path(raw_name)
        return path.rsplit("/", 1)[-1]

    def current_listing(self) -> list[str]:
        if self.state.current_dir != "/":
            prefix = self.state.current_dir.rstrip("/") + "/"
            return sorted(path[len(prefix):] for path in self.files if path.startswith(prefix))
        return sorted(path[1:] for path in self.files if path.count("/") == 1)

    def apply_pending_baud(self) -> None:
        if self.state.pending_baud is None:
            return
        time.sleep(0.2)
        self.port.set_baud(self.state.pending_baud)
        self.logger.event("EMU", "baud", f"switched to {self.state.pending_baud}")
        self.state.pending_baud = None

    def handle_command(self, line: str) -> None:
        stripped = line.strip()
        self.logger.event("RX", "command", stripped)
        if self.state.echo_enabled:
            self.send(line.encode("latin1"))

        if not stripped:
            self.send_text("\r\n")
            self.write_prompt()
            return

        parts = stripped.split()
        command = parts[0].lower()
        args = parts[1:]

        if command == "?":
            self.send_text("\r\nnew append write rm size read cat ls md cd sync stats reset init disk baud set verbose ?")
            self.send_text("\r\necho on|off, verbose on|off, rm/ls support * and ?, set 3=resetlog, stats reset")
        elif command == "append" and args:
            key = self.file_key(args[0])
            self.files.setdefault("/" + key if not key.startswith("/") else key, bytearray())
            self.state.current_file = "/" + key if not key.startswith("/") else key
            self.state.mode = "record"
            self.send_text("\r\n")
            self.write_prompt()
            return
        elif command == "new" and args:
            key = self.file_key(args[0])
            self.files["/" + key if not key.startswith("/") else key] = bytearray()
            self.send_text("\r\n")
        elif command == "write" and args:
            key = self.file_key(args[0])
            self.files.setdefault("/" + key if not key.startswith("/") else key, bytearray())
            self.state.current_file = "/" + key if not key.startswith("/") else key
            self.state.mode = "write"
            self.state.write_buffer.clear()
            self.send_text("\r\n")
            return
        elif command == "size" and args:
            key = "/" + self.file_key(args[0])
            size = len(self.files.get(key, bytearray()))
            self.send_text(f"\r\n{size}")
        elif command == "read" and args:
            key = "/" + self.file_key(args[0])
            start = int(args[1]) if len(args) > 1 else 0
            length = int(args[2]) if len(args) > 2 else len(self.files.get(key, bytearray()))
            payload = bytes(self.files.get(key, bytearray()))[start:start + length]
            self.send_text("\r\n")
            self.send(payload, kind="read-data")
        elif command == "cat" and args:
            key = "/" + self.file_key(args[0])
            payload = bytes(self.files.get(key, bytearray()))
            self.send_text("\r\n")
            self.send(payload, kind="cat-data")
        elif command == "ls":
            entries = self.current_listing()
            self.send_text("\r\n")
            if entries:
                for entry in entries:
                    self.send_text(entry + "\r\n")
            else:
                self.send_text("<empty>\r\n")
        elif command == "rm" and args:
            key = "/" + self.file_key(args[-1])
            self.files.pop(key, None)
            self.send_text("\r\n")
        elif command == "md" and args:
            path = self.normalize_path(args[0])
            self.directories.add(path)
            self.send_text("\r\n")
        elif command == "cd":
            if not args:
                self.send_text("\r\n" + self.state.current_dir)
            else:
                path = self.normalize_path(args[0])
                if path in self.directories or path == "/":
                    self.state.current_dir = path
                    self.send_text("\r\n")
                else:
                    self.report_error("error: directory not found")
        elif command == "sync":
            self.send_text("\r\nsynced")
        elif command == "init":
            self.send_text("\r\nreinitializing")
        elif command == "reset":
            self.send_text("\r\nresetting")
        elif command == "disk":
            self.send_text("\r\nMID: 03 OID: SD PNM: EMUL8 SN: 12345678 DATE: 05/2026 SIZE: 1024MB")
        elif command == "echo" and args:
            self.state.echo_enabled = args[0].lower() == "on"
            self.send_text("\r\n")
        elif command == "verbose" and args:
            self.state.verbose_errors = args[0].lower() == "on"
            self.send_text("\r\n")
        elif command == "stats":
            self.send_text("\r\nemu files=" + str(len(self.files)))
        elif command == "baud":
            if args:
                try:
                    new_baud = int(args[0])
                except ValueError:
                    self.report_error("error: invalid baud")
                else:
                    if new_baud not in BAUD_MAP:
                        self.report_error("error: invalid baud")
                    else:
                        self.state.pending_baud = new_baud
                        self.send_text("\r\nbaud changed")
            else:
                self.send_text("\r\nenter baud rate, x to exit")
        elif command == "set":
            self.send_text("\r\n0 newlog, 1 seqlog, 2 command, 3 resetlog, x exit")
        else:
            self.report_error("error: unknown command")

        self.send_text("\r\n")
        self.write_prompt()
        self.apply_pending_baud()

    def handle_write_byte(self, byte: int) -> None:
        if byte in (0x0D, 0x0A):
            line = self.state.write_buffer.decode("latin1", errors="replace")
            self.logger.event("RX", "write-line", line)
            if not line:
                self.state.mode = "command"
                self.state.write_buffer.clear()
                self.send_text("\r\n")
                self.write_prompt()
                return
            if self.state.current_file is not None:
                self.files.setdefault(self.state.current_file, bytearray()).extend(self.state.write_buffer + b"\n")
            self.state.write_buffer.clear()
            self.send_text("\r\n")
            return
        self.state.write_buffer.append(byte)

    def handle_record_byte(self, byte: int) -> None:
        if byte == ESCAPE_CHAR:
            self.state.escape_count += 1
            if self.state.escape_count >= ESCAPE_COUNT:
                self.logger.event("RX", "escape", "record-mode exit")
                self.switch_to_command_mode()
            return

        if self.state.escape_count != 0:
            if self.state.current_file is not None:
                self.files.setdefault(self.state.current_file, bytearray()).extend(
                    bytes([ESCAPE_CHAR]) * self.state.escape_count
                )
            self.state.escape_count = 0

        if self.state.current_file is not None:
            self.files.setdefault(self.state.current_file, bytearray()).append(byte)

    def handle_command_byte(self, byte: int) -> None:
        if byte in (0x0D, 0x0A):
            if self.state.command_buffer:
                line = self.state.command_buffer.decode("latin1", errors="replace")
                self.state.command_buffer.clear()
                self.handle_command(line + "\r")
            else:
                self.send_text("\r\n")
                self.write_prompt()
        else:
            self.state.command_buffer.append(byte)

    def run(self) -> int:
        signal.signal(signal.SIGINT, self.stop)
        signal.signal(signal.SIGTERM, self.stop)
        self.logger.event("EMU", "start", f"device={self.port.device} baud={self.port.baud}")
        self.boot_banner()
        while self.running:
            chunk = self.port.read()
            if not chunk:
                continue
            self.logger.raw_rx(chunk)
            self.logger.event("RX", "raw", chunk.hex(), {"length": len(chunk)})
            for byte in chunk:
                if self.state.mode == "record":
                    self.handle_record_byte(byte)
                elif self.state.mode == "write":
                    self.handle_write_byte(byte)
                else:
                    self.handle_command_byte(byte)
        self.logger.event("EMU", "stop", "terminated")
        return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Humplog serial emulator")
    parser.add_argument("--port", required=True, help="serial device path, for example /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="initial baud rate")
    parser.add_argument(
        "--boot-mode",
        choices=("command", "record"),
        default="record",
        help="initial prompt mode; record matches original Humplog default boot behavior",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="directory for raw/event logs, default: tests/emulator_logs/<timestamp>",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    timestamp = time.strftime("%Y%m%d-%H%M%S", time.localtime())
    output_dir = Path(args.output_dir) if args.output_dir else Path("tests/emulator_logs") / timestamp
    port = SerialPort(args.port, args.baud)
    logger = SessionLogger(output_dir)
    emulator = HumplogEmulator(port, logger, args.boot_mode)
    try:
        print(f"[INFO] emulating Humplog on {args.port} @ {args.baud}")
        print(f"[INFO] logs: {output_dir}")
        return emulator.run()
    finally:
        logger.write_summary()
        logger.close()
        port.close()


if __name__ == "__main__":
    sys.exit(main())
