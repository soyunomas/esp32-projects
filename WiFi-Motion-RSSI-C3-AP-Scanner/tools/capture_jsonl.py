#!/usr/bin/env python3
"""Capture versioned JSONL telemetry from the ESP32-C3 USB serial port."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import select
import sys
import termios
import time

SCHEMA = "wifi_ap_scan/v1"


class ScanBundleCollector:
    def __init__(self) -> None:
        self.pending_aps: dict[int, list[dict[str, object]]] = {}
        self.incomplete_scans = 0

    def accept(
        self, record: dict[str, object]
    ) -> list[dict[str, object]]:
        record_type = record.get("type")
        scan_id = record.get("scan_id")
        if record_type == "ap" and isinstance(scan_id, int):
            self.pending_aps.setdefault(scan_id, []).append(record)
            return []
        if record_type == "scan" and isinstance(scan_id, int):
            aps = self.pending_aps.pop(scan_id, [])
            self.pending_aps.clear()
            expected = record.get("emitted_aps")
            if isinstance(expected, int) and len(aps) == expected:
                return [*aps, record]
            self.incomplete_scans += 1
            return []
        if record_type == "scan_error":
            if isinstance(scan_id, int):
                self.pending_aps.pop(scan_id, None)
            return [record]
        return [record]


def parse_telemetry_line(raw_line: bytes) -> dict[str, object] | None:
    line = raw_line.strip()
    if not line.startswith(b"{"):
        return None
    try:
        record = json.loads(line)
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(record, dict) or record.get("schema") != SCHEMA:
        return None
    return record


def configure_port(file_descriptor: int) -> None:
    attributes = termios.tcgetattr(file_descriptor)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attributes[3] = 0
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(file_descriptor, termios.TCSANOW, attributes)


def capture(port: Path, output: Path, duration_seconds: float | None) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(
        port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK
    )
    configure_port(descriptor)
    pending = bytearray()
    scans = 0
    records = 0
    collector = ScanBundleCollector()
    deadline = (
        time.monotonic() + duration_seconds
        if duration_seconds is not None
        else None
    )
    try:
        with output.open("a", encoding="utf-8", buffering=1) as destination:
            print(f"Capturando {port} -> {output}", file=sys.stderr)
            while True:
                if deadline is not None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    wait_seconds = min(1.0, remaining)
                else:
                    wait_seconds = 1.0
                readable, _, _ = select.select(
                    [descriptor], [], [], wait_seconds
                )
                if not readable:
                    continue
                chunk = os.read(descriptor, 4096)
                if not chunk:
                    continue
                pending.extend(chunk)
                while b"\n" in pending:
                    raw_line, _, remainder = pending.partition(b"\n")
                    pending = bytearray(remainder)
                    record = parse_telemetry_line(raw_line)
                    if record is None:
                        continue
                    accepted_records = collector.accept(record)
                    for accepted in accepted_records:
                        destination.write(
                            json.dumps(
                                accepted,
                                ensure_ascii=False,
                                separators=(",", ":"),
                            )
                            + "\n"
                        )
                        records += 1
                    if any(
                        accepted.get("type") == "scan"
                        for accepted in accepted_records
                    ):
                        scans += 1
                        print(
                            f"\rescaneos={scans} registros={records}",
                            end="",
                            file=sys.stderr,
                            flush=True,
                        )
    except KeyboardInterrupt:
        pass
    finally:
        print(
            f"\nCaptura terminada: escaneos={scans}, registros={records}, "
            f"incompletos_descartados={collector.incomplete_scans}",
            file=sys.stderr,
        )
        os.close(descriptor)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--duration",
        type=float,
        help="Finaliza limpiamente tras este número de segundos",
    )
    arguments = parser.parse_args()
    if arguments.duration is not None and arguments.duration <= 0:
        parser.error("--duration debe ser mayor que cero")
    capture(arguments.port, arguments.output, arguments.duration)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
