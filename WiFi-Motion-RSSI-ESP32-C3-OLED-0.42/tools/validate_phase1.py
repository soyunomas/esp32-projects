#!/usr/bin/env python3
"""Validate a phase-1 CSV capture and summarize observed RSSI cadence."""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from pathlib import Path

HEADER_PREFIX = "uptime_ms,sample_ok,"
MINIMUM_DURATION_MS = 15 * 60 * 1000
MAXIMUM_ERROR_RUN_MS = 5 * 1000


def load_rows(path: Path) -> list[dict[str, str]]:
    header: list[str] | None = None
    rows: list[dict[str, str]] = []

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header_index = raw_line.find(HEADER_PREFIX)
        if header_index >= 0:
            header = next(csv.reader([raw_line[header_index:]]))
            continue
        if header is None:
            continue

        line = raw_line.strip()
        if not line or not line[0].isdigit():
            continue
        values = next(csv.reader([line]))
        if len(values) == len(header):
            rows.append(dict(zip(header, values, strict=True)))

    if header is None:
        raise ValueError("no se encontró la cabecera CSV de fase 1")
    if not rows:
        raise ValueError("no se encontraron muestras de fase 1")
    return rows


def integer(row: dict[str, str], name: str) -> int:
    try:
        return int(row[name])
    except (KeyError, ValueError) as error:
        raise ValueError(f"campo inválido o ausente: {name}") from error


def percentile_50(values: list[int]) -> str:
    return f"{statistics.median(values):.1f}" if values else "n/d"


def validate(rows: list[dict[str, str]]) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    summary: list[str] = []
    uptimes = [integer(row, "uptime_ms") for row in rows]
    duration_ms = uptimes[-1] - uptimes[0]
    resets = sum(current < previous for previous, current in zip(uptimes, uptimes[1:]))

    successful = sum(integer(row, "sample_ok") == 1 for row in rows)
    errors = len(rows) - successful
    repeated = integer(rows[-1], "repeated_samples")
    changes = integer(rows[-1], "rssi_changes")
    schedule_misses = integer(rows[-1], "schedule_misses")
    disconnects = integer(rows[-1], "disconnects")
    reconnects = integer(rows[-1], "reconnects")

    maximum_error_run_ms = 0
    error_run_start: int | None = None
    for uptime, row in zip(uptimes, rows):
        if integer(row, "sample_ok") == 0:
            if error_run_start is None:
                error_run_start = uptime
            maximum_error_run_ms = max(maximum_error_run_ms,
                                       uptime - error_run_start)
        else:
            error_run_start = None

    query_intervals = [
        integer(row, "query_interval_ms")
        for row in rows
        if integer(row, "query_interval_ms") > 0
    ]
    change_intervals = [
        integer(row, "rssi_change_interval_ms")
        for row in rows
        if integer(row, "rssi_changed") == 1
        and integer(row, "rssi_change_interval_ms") > 0
    ]
    repetition_percent = 100.0 * repeated / successful if successful else 0.0

    summary.extend([
        f"duración: {duration_ms / 1000:.1f} s",
        f"registros: {len(rows)} ({successful} correctos, {errors} errores)",
        f"intervalo de consulta mediano: {percentile_50(query_intervals)} ms",
        f"intervalo entre cambios RSSI mediano: {percentile_50(change_intervals)} ms",
        f"RSSI repetido: {repeated}/{successful} ({repetition_percent:.1f} %)",
        f"cambios RSSI observados: {changes}",
        f"pérdidas de planificación estimadas: {schedule_misses}",
        f"desconexiones/reconexiones: {disconnects}/{reconnects}",
        f"racha máxima de errores: {maximum_error_run_ms} ms",
        f"reinicios detectados: {resets}",
    ])

    if duration_ms < MINIMUM_DURATION_MS:
        failures.append(
            f"duración inferior a 15 minutos ({duration_ms / 1000:.1f} s)"
        )
    if resets:
        failures.append(f"se detectaron {resets} reinicios")
    if successful == 0:
        failures.append("no hay muestras RSSI correctas")
    if maximum_error_run_ms > MAXIMUM_ERROR_RUN_MS:
        failures.append(
            f"racha de errores superior a {MAXIMUM_ERROR_RUN_MS} ms"
        )
    return summary, failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path, help="captura CSV del monitor serie")
    args = parser.parse_args()

    try:
        rows = load_rows(args.capture)
        summary, failures = validate(rows)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    print("Resumen de fase 1")
    for item in summary:
        print(f"- {item}")
    if failures:
        print("Resultado: NO APTO")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Resultado: APTO")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
