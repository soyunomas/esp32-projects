#!/usr/bin/env python3
"""Capture compact raw CSI records from the ESP32 serial stream into JSONL."""
from __future__ import annotations
import argparse, json, sys, time
from pathlib import Path
import serial
PREFIX = "CSI_RAW_V1"
def parse_raw_line(line: str) -> dict | None:
    if not line.startswith(PREFIX + ","):
        return None
    parts = line.strip().split(",", 6)
    if len(parts) != 7:
        return None
    _, received_us, wifi_us, rssi, first_invalid, length, hex_payload = parts
    try:
        expected_length = int(length)
        payload = bytes.fromhex(hex_payload)
        if len(payload) != expected_length:
            return None
        return {"received_us": int(received_us), "wifi_timestamp_us": int(wifi_us), "rssi_dbm": int(rssi), "first_word_invalid": bool(int(first_invalid)), "length": expected_length, "csi_hex": hex_payload.upper()}
    except (ValueError, TypeError):
        return None

def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--label", required=True)
    p.add_argument("--session", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--duration", type=float, default=60.0, help="seconds; 0 means until Ctrl-C")
    a = p.parse_args()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic(); captured = malformed = 0
    try:
        with serial.Serial(a.port, a.baud, timeout=1) as ser, a.output.open("w", encoding="utf-8") as out:
            print(f"Capturing {a.label!r} session {a.session!r} from {a.port} -> {a.output}")
            while a.duration <= 0 or time.monotonic() - start < a.duration:
                raw = ser.readline()
                if not raw: continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line.startswith(PREFIX + ","): continue
                rec = parse_raw_line(line)
                if rec is None:
                    malformed += 1; continue
                rec["label"] = a.label; rec["session"] = a.session
                out.write(json.dumps(rec, separators=(",", ":")) + "\n")
                captured += 1
                if captured % 100 == 0: print(f"frames={captured} malformed={malformed}", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    print(f"Done: frames={captured}, malformed={malformed}")
    return 0 if captured else 2
if __name__ == "__main__": raise SystemExit(main())
