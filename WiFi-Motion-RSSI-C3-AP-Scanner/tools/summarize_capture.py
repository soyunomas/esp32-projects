#!/usr/bin/env python3
"""Summarize phase-1 AP scan captures without selecting thresholds."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics
from typing import Any, Iterable

SCHEMA = "wifi_ap_scan/v1"


def median_absolute_deviation(values: list[int]) -> float:
    if not values:
        return 0.0
    center = statistics.median(values)
    return float(statistics.median(abs(value - center) for value in values))


def nearest_rank_percentile(values: list[int], percentile: float) -> float:
    if not values:
        raise ValueError("se necesita al menos un valor")
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile * len(ordered)))
    return float(ordered[rank - 1])


def summarize(records: Iterable[dict[str, Any]]) -> dict[str, Any]:
    valid = [record for record in records if record.get("schema") == SCHEMA]
    scans = [record for record in valid if record.get("type") == "scan"]
    aps = [record for record in valid if record.get("type") == "ap"]
    errors = [record for record in valid if record.get("type") == "scan_error"]
    markers = [record for record in valid if record.get("type") == "marker"]

    durations = [
        int(record["duration_ms"])
        for record in scans
        if isinstance(record.get("duration_ms"), int)
    ]
    free_heap = [
        int(record["free_heap"])
        for record in scans
        if isinstance(record.get("free_heap"), int)
    ]
    minimum_heap = [
        int(record["minimum_free_heap"])
        for record in scans
        if isinstance(record.get("minimum_free_heap"), int)
    ]

    by_bssid: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in aps:
        bssid = record.get("bssid")
        if isinstance(bssid, str) and bssid:
            by_bssid[bssid].append(record)

    references = []
    scan_count = len(scans)
    ordered_scan_ids = [
        record["scan_id"]
        for record in scans
        if isinstance(record.get("scan_id"), int)
    ]
    scan_positions = {
        scan_id: position
        for position, scan_id in enumerate(ordered_scan_ids)
    }
    split_index = len(ordered_scan_ids) // 2
    first_half_ids = set(ordered_scan_ids[:split_index])
    second_half_ids = set(ordered_scan_ids[split_index:])
    for bssid in sorted(by_bssid):
        observations = by_bssid[bssid]
        rssis = [
            int(record["rssi"])
            for record in observations
            if isinstance(record.get("rssi"), int)
        ]
        observed_scan_ids = {
            record["scan_id"]
            for record in observations
            if isinstance(record.get("scan_id"), int)
        }
        ssids = sorted(
            {
                str(record["ssid"])
                for record in observations
                if isinstance(record.get("ssid"), str)
            }
        )
        channels = sorted(
            {
                int(record["channel"])
                for record in observations
                if isinstance(record.get("channel"), int)
            }
        )
        first_half_rssis = [
            int(record["rssi"])
            for record in observations
            if record.get("scan_id") in first_half_ids
            and isinstance(record.get("rssi"), int)
        ]
        second_half_rssis = [
            int(record["rssi"])
            for record in observations
            if record.get("scan_id") in second_half_ids
            and isinstance(record.get("rssi"), int)
        ]
        first_half_median = (
            float(statistics.median(first_half_rssis))
            if first_half_rssis
            else None
        )
        second_half_median = (
            float(statistics.median(second_half_rssis))
            if second_half_rssis
            else None
        )
        ordered_observations = sorted(
            (
                record
                for record in observations
                if record.get("scan_id") in scan_positions
                and isinstance(record.get("rssi"), int)
            ),
            key=lambda record: scan_positions[record["scan_id"]],
        )
        adjacent_deltas = [
            abs(int(current["rssi"]) - int(previous["rssi"]))
            for previous, current in zip(
                ordered_observations, ordered_observations[1:]
            )
            if scan_positions[current["scan_id"]]
            - scan_positions[previous["scan_id"]]
            == 1
        ]
        references.append(
            {
                "bssid": bssid,
                "ssids": ssids,
                "channels": channels,
                "samples": len(rssis),
                "presence_ratio": (
                    len(observed_scan_ids) / scan_count if scan_count else 0.0
                ),
                "rssi_median": (
                    float(statistics.median(rssis)) if rssis else None
                ),
                "rssi_mad": (
                    median_absolute_deviation(rssis) if rssis else None
                ),
                "rssi_min": min(rssis) if rssis else None,
                "rssi_max": max(rssis) if rssis else None,
                "adjacent_rssi_change": {
                    "samples": len(adjacent_deltas),
                    "median_abs": (
                        float(statistics.median(adjacent_deltas))
                        if adjacent_deltas
                        else None
                    ),
                    "p90_abs": (
                        nearest_rank_percentile(adjacent_deltas, 0.90)
                        if adjacent_deltas
                        else None
                    ),
                    "max_abs": (
                        max(adjacent_deltas) if adjacent_deltas else None
                    ),
                },
                "half_comparison": {
                    "first_samples": len(first_half_rssis),
                    "second_samples": len(second_half_rssis),
                    "first_median": first_half_median,
                    "second_median": second_half_median,
                    "median_shift": (
                        second_half_median - first_half_median
                        if first_half_median is not None
                        and second_half_median is not None
                        else None
                    ),
                },
            }
        )

    return {
        "schema": "wifi_ap_scan_summary/v1",
        "scan_count": scan_count,
        "ap_observation_count": len(aps),
        "scan_error_count": len(errors),
        "marker_count": len(markers),
        "truncated_scan_count": sum(
            record.get("truncated") is True for record in scans
        ),
        "dropped_events_max": max(
            (
                int(record["dropped_events"])
                for record in scans
                if isinstance(record.get("dropped_events"), int)
            ),
            default=0,
        ),
        "duration_ms": {
            "min": min(durations) if durations else None,
            "median": (
                float(statistics.median(durations)) if durations else None
            ),
            "max": max(durations) if durations else None,
        },
        "heap": {
            "free_min": min(free_heap) if free_heap else None,
            "free_max": max(free_heap) if free_heap else None,
            "minimum_recorded": min(minimum_heap) if minimum_heap else None,
        },
        "references": references,
    }


def load_records(path: Path) -> list[dict[str, Any]]:
    records = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, start=1):
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"{path}:{line_number}: JSON inválido: {error}"
                ) from error
            if not isinstance(record, dict):
                raise ValueError(
                    f"{path}:{line_number}: se esperaba un objeto JSON"
                )
            records.append(record)
    return records


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        help="Guarda el resumen; por defecto lo imprime",
    )
    arguments = parser.parse_args()
    result = summarize(load_records(arguments.capture))
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if arguments.output is None:
        print(encoded, end="")
    else:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(encoded, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
